/* ==========================================================================
 *  reipan_paths.cpp  --  the three roots, resolved once
 *
 *  See reipan_paths.h for the layout and why it is that layout.
 *
 *  This replaces a twenty-candidate probe that ran per file open: the resolver
 *  used to try $REIPAN_DATA_DIR, the executable's directory and seven of its
 *  ancestors, ".", "..", "../..", "../../..", "data", "../data" and
 *  "../../data", each with two path variants, and took the first that opened.
 *  Three things were wrong with it.  It cost up to sixty failed opens per miss,
 *  on a loader that misses often by design.  It was silent -- a stray bin/ in
 *  any ancestor directory won, and the game read the wrong data with no
 *  diagnostic.  And it could not be pointed anywhere, which is the only thing
 *  that helps on a platform where the data is not near the binary at all.
 *
 *  Now each root is decided once, logged, and joined to directly.  Discovery
 *  still walks the same neighbourhood so a build tree needs no configuration,
 *  but a candidate only wins by holding a sentinel that says what it is, and
 *  the answer is written into reipan.ini so it is visible and editable.
 *
 *  PORT DEVIATION, deliberate, same as libmc.cpp: this file uses
 *  std::filesystem directly rather than going through reipan_file.  It has to
 *  test for directories and create them, and reipan_file is a stream API with
 *  no notion of either.  File existence still goes through SDL so that an
 *  Android APK asset -- which is not a filesystem path at all -- answers.
 * ======================================================================== */

#include "reipan_paths.h"

#include "reipan_file.h"
#include "reipan_iso9660.h"
#include "../reipan_config.h"
#include "system/eeiop/cddat.h"     // CDDAT_HEADER, CD_FILE_DAT_NUM

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <cstdlib>
#include <cstring>

namespace
{

/* Android hands SDL_GetBasePath() an "assets://" URL rather than a path: the
 * shipped resources live inside the APK and only SDL's I/O layer can open
 * them.  Anything that would hand such a string to std::filesystem has to be
 * skipped, so it is worth one predicate. */
bool IsAssetUrl(const char *path)
{
    return path != nullptr && std::strncmp(path, "assets://", 9) == 0;
}

bool IsSlash(char ch)
{
    return ch == '/' || ch == '\\';
}

/* Existence through SDL, so an APK asset and a real file both answer. */
bool FileExists(const std::string &path)
{
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
    if (io == nullptr)
    {
        return false;
    }
    SDL_CloseIO(io);
    return true;
}

bool DirExists(const std::string &path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

/* Through SDL, because this is asked of paths the player chose, and it reads
 * them as UTF-8 where a narrow std::filesystem path would not. */
bool IsFile(const std::string &path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string ForwardSlashes(std::string path)
{
    for (char &ch : path)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
    return path;
}

/*
 * One separator, one trailing slash: '/' throughout, exactly one at the end, so
 * every join is a plain concatenation.
 *
 * Forward slashes on Windows too.  Win32 accepts them everywhere, and the
 * alternative is a data_folder line in reipan.ini reading "C:\games\ff2/",
 * which is what mixing SDL_GetBasePath's separator with ours produces.  A path
 * the player is expected to edit should not look like that.
 */
std::string WithTrailingSlash(std::string path)
{
    if (path.empty())
    {
        return "./";
    }
    for (char &ch : path)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
    while (path.size() > 1 && path.back() == '/')
    {
        path.pop_back();
    }
    if (path.back() != '/')
    {
        path += '/';
    }
    return path;
}

/*
 * Normalize a PS2-style name: drop the "device:" prefix, any leading slashes
 * and the ";version" suffix, and turn '\' into '/'.  What comes back is
 * relative to whichever root the caller is about to join it to.
 */
std::string NormalizePs2Path(const char *name)
{
    if (name == nullptr)
    {
        return std::string();
    }

    const char *src = std::strchr(name, ':');
    src = (src != nullptr) ? src + 1 : name;

    while (IsSlash(*src))
    {
        src++;
    }

    std::string out;
    for (; *src != '\0' && *src != ';'; src++)
    {
        out += (*src == '\\') ? '/' : *src;
    }
    return out;
}

/*
 * Reduce a caller-supplied name to a relative path that cannot leave the root
 * it is about to be joined to: no drive letter, no leading separator, no "."
 * or ".." component.  Used for the user directory, where the names come from
 * the ROM ("host0:../bin/data/scene/sceneXX.slt") and must not write outside
 * it.
 */
std::string SanitizeRelative(const char *name)
{
    std::string flat = NormalizePs2Path(name);
    std::string out;
    std::size_t i = 0;

    while (i < flat.size())
    {
        const std::size_t end = flat.find('/', i);
        const std::string part =
            flat.substr(i, end == std::string::npos ? std::string::npos : end - i);

        if (!part.empty() && part != "." && part != "..")
        {
            if (!out.empty())
            {
                out += '/';
            }
            out += part;
        }

        if (end == std::string::npos)
        {
            break;
        }
        i = end + 1;
    }

    return out;
}

/* Drop the last component of a directory path.  Empty when there is nothing
 * left to drop, which is how the ancestor walk terminates. */
std::string ParentOf(const std::string &path)
{
    std::size_t len = path.size();

    while (len > 0 && IsSlash(path[len - 1]))
    {
        len--;
    }
    while (len > 0 && !IsSlash(path[len - 1]))
    {
        len--;
    }
    while (len > 0 && IsSlash(path[len - 1]))
    {
        len--;
    }

    return (len == 0) ? std::string() : path.substr(0, len);
}

/*
 * The neighbourhood a root can be discovered in: the executable's directory
 * and its ancestors, then the working directory and its ancestors.
 *
 * The executable's directory comes first and is the one that does not move.
 * The working directory is anything at all -- a multi-config generator adds a
 * per-configuration level, and an IDE, debugger or launcher sets it to
 * whatever it likes -- so it is a fallback, not a basis.
 */
void AppendSearchRoots(std::vector<std::string> *out)
{
    const char *base = SDL_GetBasePath();

    if (base != nullptr && base[0] != '\0' && !IsAssetUrl(base))
    {
        std::string dir = base;
        for (int level = 0; level < 8 && !dir.empty(); level++)
        {
            out->push_back(dir);
            dir = ParentOf(dir);
        }
    }

    std::string cwd = ".";
    for (int level = 0; level < 4; level++)
    {
        out->push_back(cwd);
        cwd += "/..";
    }
}

/* ---- the user directory ------------------------------------------------- */

/*
 * Carry a build tree's settings and card over the one time the destination is
 * empty.
 *
 * Until this module existed both lived beside the executable, and moving where
 * they are read from without moving the files themselves would present as the
 * player's saves and settings having vanished.  Guarded on the destination
 * being absent, so it runs once and never overwrites anything.
 */
void MigrateLegacyUserData(const std::string &user_dir)
{
    const char *base = SDL_GetBasePath();

    if (base == nullptr || base[0] == '\0' || IsAssetUrl(base))
    {
        return;
    }

    const std::filesystem::path legacy(base);
    const std::filesystem::path target(user_dir);
    std::error_code ec;

    if (std::filesystem::equivalent(legacy, target, ec))
    {
        return;
    }

    struct Item
    {
        const char *name;
        bool        directory;
    };
    static const Item items[] = {
        {"reipan.ini", false},
        {"memcard", true},
    };

    for (const Item &item : items)
    {
        const std::filesystem::path from = legacy / item.name;
        const std::filesystem::path to   = target / item.name;

        if (std::filesystem::exists(to, ec) || !std::filesystem::exists(from, ec))
        {
            continue;
        }

        if (item.directory)
        {
            std::filesystem::copy(from, to,
                                  std::filesystem::copy_options::recursive, ec);
        }
        else
        {
            std::filesystem::copy_file(from, to, ec);
        }

        if (ec)
        {
            SDL_Log("ReiPan paths: could not carry %s over from %s: %s",
                    item.name, base, ec.message().c_str());
            ec.clear();
        }
        else
        {
            SDL_Log("ReiPan paths: carried %s over from %s", item.name, base);
        }
    }
}

/*
 * Each of the three roots is computed exactly once, by whichever thread asks
 * first, through a function-local static.  That initialization is serialized by
 * the language, and it has to be: the IOP emulation reads game data on its own
 * thread (MyCdRead() -> sceCdRead()), so the root can genuinely be asked for
 * from two threads at the same time.  A hand-rolled "if (!dir.empty())" cache
 * would be a data race here, not merely a redundant computation.
 */
std::string ComputeUserDir()
{
    std::string dir;

    /* A scripted run keeps its saves and settings apart from the player's. */
    const char *override_dir = std::getenv("REIPAN_USER_DIR");
    const bool  overridden = override_dir != nullptr && override_dir[0] != '\0';
#ifdef REIPAN_SLPS_25544_CHS
    const char *profile = "ReiPan-CHS";
#else
    const char *profile = "ReiPan";
#endif
    char *pref = overridden ? nullptr : SDL_GetPrefPath("Mikompilation", profile);
    if (overridden)
    {
        dir = WithTrailingSlash(override_dir);
    }
    else if (pref != nullptr && pref[0] != '\0')
    {
        dir = WithTrailingSlash(pref);
        SDL_free(pref);
    }
    else
    {
        if (pref != nullptr)
        {
            SDL_free(pref);
        }
        /* SDL could not name a per-user directory (no HOME, a sandbox with
         * nothing writable).  A relative directory keeps the writers working
         * rather than turning every save into a failure. */
        dir = "ReiPan/";
        SDL_Log("ReiPan paths: SDL_GetPrefPath failed (%s); writing under %s",
                SDL_GetError(), dir.c_str());
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    MigrateLegacyUserData(dir);

    SDL_Log("ReiPan paths: user   = %s", dir.c_str());
    return dir;
}

const std::string &UserDir()
{
    static const std::string dir = ComputeUserDir();
    return dir;
}

/* ---- the data root ----------------------------------------------------- */

/* A disc copied file by file can keep the ISO 9660 version suffix in its host
 * names ("IMG_BD.BIN;1", "ZERO3.FHD;1").  The plain name wins; the suffixed one
 * is the fallback.  Returns the spelling that exists, or an empty string. */
std::string ExistingIsoSpelling(const std::string &path)
{
    if (FileExists(path))
    {
        return path;
    }
    const std::string versioned = path + ";1";
    if (FileExists(versioned))
    {
        return versioned;
    }
    return std::string();
}

/*
 * What makes a directory the game's: the file table ZERO3.FHD, which ee_iopInit
 * reads before anything else can start and which is on no copy of any other game,
 * with what it describes beside it -- the disc's IMG_BD.BIN or either spelling of
 * an extracted tree.  Each file with or without its ";1".
 */
int FindGameBeside(const std::string &archive, const std::string &folder);

bool LooksLikeDataRoot(const std::string &dir)
{
    const std::string root = WithTrailingSlash(dir);

#ifdef REIPAN_SLPS_25544_CHS
    const std::string archive = ExistingIsoSpelling(root + "IMG_BD.BIN");
    return !archive.empty() && FindGameBeside(archive, root) == REIPAN_GAMEFILE_OK;
#else
    if (ExistingIsoSpelling(root + "ZERO3.FHD").empty())
    {
        return false;
    }
    return !ExistingIsoSpelling(root + "IMG_BD.BIN").empty() ||
           DirExists(root + "bin/data") ||
           DirExists(root + "bin2/data");
#endif
}

/*
 * The names the disc's files go by while they are read out of a disc image, this
 * followed by the name in the disc's root.  No host file has them.  Everything
 * that reads one -- the CD layer, the loader, the movie stream, sceOpen -- gets it
 * from ReiPan_PathResolveData() and opens it through reipan_file, where it is a
 * region of the image.  FF2's port needs only the archive; FF3 reads its file
 * table off the disc as well, so the whole root is mapped.
 */
constexpr const char *kDiscPrefix = "disc:/";

/* Set with the root, under the same guard, and read after it. */
bool g_playing_from_disc = false;
std::vector<std::string> g_disc_files;

/* The largest ZERO3.FHD worth reading: the EU disc's is under 300 KB. */
constexpr uint64_t kFileTableMax = 16u << 20;

uint32_t TableWord(const std::vector<unsigned char> &table, uint64_t offset)
{
    uint32_t value;
    std::memcpy(&value, table.data() + offset, sizeof(value));
    return value;
}

/*
 * Is `table` a ZERO3.FHD this port can play, of an archive `archive_size` bytes
 * long?  Game code names files by the numbers in cd_file_dat.h, which were taken
 * from the European disc, so a table with any other count is another release
 * whose numbers mean other files.  An entry reaching past the archive means the
 * archive is not all there.  (A label has no file: size -1, and sector -1.)
 */
int CheckFileTable(const std::vector<unsigned char> &table, uint64_t archive_size)
{
    CDDAT_HEADER header;
    if (table.size() < sizeof(header))
    {
        return REIPAN_GAMEFILE_OTHER_RELEASE;
    }
    std::memcpy(&header, table.data(), sizeof(header));

#ifdef REIPAN_SLPS_25544_CHS
    if (header._028[0] != 0x53484352u)
        return REIPAN_GAMEFILE_OTHER_RELEASE;
#endif
    const uint64_t count = (uint64_t)CD_FILE_DAT_NUM;
    const uint64_t tables = count * sizeof(uint32_t);
    if (header.magic != CDDAT_FHD_MAGIC || header.file_num != CD_FILE_DAT_NUM ||
        (uint64_t)header.size_tbl + tables > table.size() ||
        (uint64_t)header.cmp_tbl + tables > table.size() ||
        (uint64_t)header.lba_tbl + tables > table.size())
    {
        return REIPAN_GAMEFILE_OTHER_RELEASE;
    }

    for (uint64_t i = 0; i < count; i++)
    {
        if (TableWord(table, header.size_tbl + i * sizeof(uint32_t)) == 0xffffffffu)
        {
            continue;
        }
        const uint64_t sector = TableWord(table, header.lba_tbl + i * sizeof(uint32_t));
        const uint64_t stored = TableWord(table, header.cmp_tbl + i * sizeof(uint32_t)) >> 1;
        if (sector * 0x800u + stored > archive_size)
        {
            return REIPAN_GAMEFILE_CUT_SHORT;
        }
    }
    return REIPAN_GAMEFILE_OK;
}

/* The game on the disc image at `path`: an image that passes this can be played
 * from.  `files` gets what is in the disc's root, if it is wanted. */
int FindGameInImage(const std::string &path, std::vector<ReiPanIso9660::Entry> *files)
{
    ReiPanIso9660::Image image;
    switch (image.Open(path))
    {
    case ReiPanIso9660::OpenResult::Ok:
        break;
    case ReiPanIso9660::OpenResult::Unreadable:
        return REIPAN_GAMEFILE_UNREADABLE;
    case ReiPanIso9660::OpenResult::NotAnImage:
        return REIPAN_GAMEFILE_NOT_AN_IMAGE;
    }

    ReiPanIso9660::Entry archive;
    ReiPanIso9660::Entry table;
    if (!image.Find("IMG_BD.BIN", archive) || archive.directory ||
        !image.Find("ZERO3.FHD", table) || table.directory)
    {
        return REIPAN_GAMEFILE_NOT_THE_GAME;
    }
    for (const ReiPanIso9660::Entry *entry : {&archive, &table})
    {
        if (entry->offset > image.Size() || entry->size > image.Size() - entry->offset)
        {
            return REIPAN_GAMEFILE_CUT_SHORT;
        }
    }
    if (table.size > kFileTableMax)
    {
        return REIPAN_GAMEFILE_OTHER_RELEASE;
    }

    std::vector<unsigned char> bytes((size_t)table.size);
    if (!image.Read(table.offset, bytes.data(), bytes.size()))
    {
        return REIPAN_GAMEFILE_UNREADABLE;
    }
    const int checked = CheckFileTable(bytes, archive.size);
    if (checked != REIPAN_GAMEFILE_OK)
    {
        return checked;
    }

    if (files != nullptr)
    {
        ReiPanIso9660::Entry root;
        image.Find("", root);
        *files = image.List(root);
    }
    return REIPAN_GAMEFILE_OK;
}

/* The same for the copy of the disc's files in `folder`, holding the IMG_BD.BIN
 * at `archive`. */
int FindGameBeside(const std::string &archive, const std::string &folder)
{
    const std::string fhd = ExistingIsoSpelling(folder + "ZERO3.FHD");
    if (fhd.empty())
    {
        return REIPAN_GAMEFILE_NO_TABLE;
    }

    const int64_t archive_size = ReiPan_FileSize(archive.c_str());
    const int64_t table_size = ReiPan_FileSize(fhd.c_str());
    if (archive_size < 0 || table_size < 0)
    {
        return REIPAN_GAMEFILE_UNREADABLE;
    }
    if ((uint64_t)table_size > kFileTableMax)
    {
        return REIPAN_GAMEFILE_OTHER_RELEASE;
    }

    std::vector<unsigned char> bytes((size_t)table_size);
    if (!ReiPan_FileReadAt(fhd.c_str(), 0, bytes.data(), bytes.size()))
    {
        return REIPAN_GAMEFILE_UNREADABLE;
    }
    const int checked = CheckFileTable(bytes, (uint64_t)archive_size);
#ifdef REIPAN_SLPS_25544_CHS
    if (checked == REIPAN_GAMEFILE_OK)
    {
        CDDAT_HEADER header;
        std::memcpy(&header, bytes.data(), sizeof(header));
        static const struct { int file; const char *name; } overrides[] = {
            { MSG_OBJ, "bin/data/msg/reipan_msg.obj" },
            { SUBTITLE_OBJ, "bin/data/subtitle/reipan_subtitle.obj" },
            { CAPTION_PHF, "bin/data/2ddata/b2d/reipan_caption.b2d" },
        };
        for (const auto &entry : overrides)
        {
            const auto size = TableWord(bytes, header.size_tbl + (uint64_t)entry.file * 4);
            if (ReiPan_FileSize((folder + entry.name).c_str()) != size)
                return REIPAN_GAMEFILE_CUT_SHORT;
        }
        if (ReiPan_FileSize((folder + "font_widths.bin").c_str()) != 2100)
            return REIPAN_GAMEFILE_CUT_SHORT;
    }
#endif
    return checked;
}

/* A file is a disc image or nothing here: IMG_BD.BIN itself is named by the
 * folder it is in. */
bool HoldsGameFiles(const std::string &where)
{
    if (IsFile(where))
    {
        return FindGameInImage(where, nullptr) == REIPAN_GAMEFILE_OK;
    }
    return LooksLikeDataRoot(where);
}

std::string DiscoverDataDir()
{
    std::vector<std::string> candidates;

    AppendSearchRoots(&candidates);

    /* A "data" subdirectory of any of those, which is how the old resolver's
     * "data", "../data" and "../../data" entries were reachable. */
    const std::size_t direct = candidates.size();
    for (std::size_t i = 0; i < direct; i++)
    {
        candidates.push_back(WithTrailingSlash(candidates[i]) + "data");
    }

    for (const std::string &candidate : candidates)
    {
        if (LooksLikeDataRoot(candidate))
        {
            return candidate;
        }
    }

    return std::string();
}

/* Which source the data root came from, for ReiPan_PathDataDirSource().  Set by
 * ComputeDataDir() on the way out, so it is written exactly once under the same
 * static-initialization guard that produces the root itself. */
const char *g_data_source = "not found";

std::string ComputeDataDir()
{
    std::string dir;

    /* [paths] data_folder is one of the three sources below, so the file has to
     * have been read before any of them is consulted.  ReiPan_ConfigLoad() needs
     * only the user directory, which is resolved independently of this one, so
     * the two cannot deadlock. */
    ReiPan_ConfigLoad();

    /*
     * The environment first, so this keeps the precedence the rest of the
     * settings have -- defaults < file < environment -- and a test run against
     * another copy of the data does not have to disturb the file to get it.
     * Deliberately NOT written back for the same reason: an override is a
     * one-off by intent, and persisting it would silently make it permanent.
     */
    const char *env = std::getenv("REIPAN_DATA_DIR");
    if (env != nullptr && env[0] != '\0')
    {
        dir = WithTrailingSlash(env);
        g_data_source = "REIPAN_DATA_DIR";
        SDL_Log("ReiPan paths: data   = %s  (from REIPAN_DATA_DIR)", dir.c_str());
        if (!LooksLikeDataRoot(dir))
        {
            SDL_Log("ReiPan paths: warning -- no ZERO3.FHD with IMG_BD.BIN or "
                    "bin/data under REIPAN_DATA_DIR; the game will not find its "
                    "files");
        }
        return dir;
    }

    const char *configured = reipan_config.paths.data_folder;

    /*
     * The setting can name a disc image.  The root is then the folder it is in,
     * which is only what the settings UI shows: every name that is not in the
     * disc's root misses, as it does in a data folder holding nothing else.
     */
    if (configured[0] != '\0' && IsFile(configured))
    {
        const std::string image = ForwardSlashes(configured);
        std::vector<ReiPanIso9660::Entry> files;
        if (FindGameInImage(image, &files) == REIPAN_GAMEFILE_OK)
        {
            for (const ReiPanIso9660::Entry &file : files)
            {
                if (!file.directory)
                {
                    ReiPan_FileMapRegion((kDiscPrefix + file.name).c_str(),
                                         image.c_str(), file.offset, file.size);
                    g_disc_files.push_back(file.name);
                }
            }
            g_playing_from_disc = true;
            dir = WithTrailingSlash(ParentOf(image));
            g_data_source = "disc image";
            SDL_Log("ReiPan paths: data   = %s  (disc image)", image.c_str());
            ReiPan_ConfigSetDataFolder(image.c_str());
            return dir;
        }
    }

    const std::string wanted =
        (configured[0] != '\0') ? WithTrailingSlash(configured) : std::string();

    if (!wanted.empty() && LooksLikeDataRoot(wanted))
    {
        dir = wanted;
        g_data_source = "reipan.ini";
        SDL_Log("ReiPan paths: data   = %s  (from reipan.ini)", dir.c_str());
        /* Write the tidied form back, so a hand-typed line acquires its
         * trailing slash and loses its backslashes once rather than looking
         * different from every other path in the file forever.  A no-op when
         * the value is already in that form. */
        ReiPan_ConfigSetDataFolder(dir.c_str());
        return dir;
    }

    /*
     * Either nothing is configured, or what is configured no longer holds the
     * game -- so look, and prefer what is found.
     *
     * Falling back rather than failing is the point.  The value in the file is
     * an absolute path that discovery itself wrote, and a build tree that moves
     * or a copy of the game handed to someone else takes every such path with
     * it.  Honouring a stale line over a data folder sitting right beside the
     * executable would turn a rename into "the game stopped working", with the
     * cause a line the player never typed.
     */
    const std::string found = DiscoverDataDir();
    if (!found.empty())
    {
        dir = WithTrailingSlash(found);
        g_data_source = "found automatically";
        if (!wanted.empty())
        {
            SDL_Log("ReiPan paths: [paths] data_folder = %s holds no ZERO3.FHD "
                    "with IMG_BD.BIN or bin/data; using %s instead", wanted.c_str(),
                    dir.c_str());
        }
        SDL_Log("ReiPan paths: data   = %s  (discovered)", dir.c_str());

        /* Record it, so the file says where the game is reading from and the
         * player can repoint it without having to guess the key. */
        ReiPan_ConfigSetDataFolder(dir.c_str());
        return dir;
    }

    /* Configured, wrong, and nothing better anywhere.  Keep what was asked for:
     * the loader's diagnostics then name the folder the player chose, which is
     * the thing they can act on. */
    if (!wanted.empty())
    {
        dir = wanted;
        g_data_source = "reipan.ini";
        SDL_Log("ReiPan paths: data   = %s  (from reipan.ini)", dir.c_str());
        SDL_Log("ReiPan paths: warning -- no ZERO3.FHD with IMG_BD.BIN or "
                "bin/data there, and none found near the executable either; the "
                "game will not find its files");
        return dir;
    }

    /* Nothing found.  The executable's directory is the honest guess and gives
     * the loader's diagnostics a sensible path to name. */
    const char *base = SDL_GetBasePath();
    dir = WithTrailingSlash((base != nullptr && base[0] != '\0') ? base : ".");
    SDL_Log("ReiPan paths: no ZERO3.FHD with IMG_BD.BIN or bin/data found near "
            "the executable or the working directory.  Set [paths] data_folder in "
            "%sreipan.ini to the folder holding the game's files, or to a disc "
            "image.", UserDir().c_str());
    SDL_Log("ReiPan paths: data   = %s  (fallback)", dir.c_str());
    return dir;
}

const std::string &DataDir()
{
    static const std::string dir = ComputeDataDir();
    return dir;
}

/* ---- the assets root --------------------------------------------------- */

std::string ComputeAssetsDir()
{
    std::string dir;

    const char *base = SDL_GetBasePath();

    /* Android: the resources are in the APK and SDL_IOFromFile reads them from
     * a bare relative path.  There is nothing to probe and nothing to log a
     * path for. */
    if (IsAssetUrl(base))
    {
        dir = "";
        SDL_Log("ReiPan paths: assets = <APK>");
        return dir;
    }

    std::vector<std::string> candidates;
    AppendSearchRoots(&candidates);

    for (const std::string &candidate : candidates)
    {
        if (DirExists(WithTrailingSlash(candidate) + "resources/shaders"))
        {
            dir = WithTrailingSlash(candidate);
            SDL_Log("ReiPan paths: assets = %s", dir.c_str());
            return dir;
        }
    }

    dir = WithTrailingSlash((base != nullptr && base[0] != '\0') ? base : ".");
    SDL_Log("ReiPan paths: assets = %s  (no resources/shaders found there -- "
            "shaders and controller mappings will be missing)", dir.c_str());
    return dir;
}

const std::string &AssetsDir()
{
    static const std::string dir = ComputeAssetsDir();
    return dir;
}

/* Copy `value` out, or fail rather than truncate: a silently shortened path is
 * a wrong path. */
int Emit(const std::string &value, char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return 0;
    }
    if (value.size() + 1 > out_size)
    {
        SDL_Log("ReiPan paths: path does not fit %zu bytes: %s", out_size,
                value.c_str());
        out[0] = '\0';
        return 0;
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return 1;
}

} // namespace

extern "C" {

const char *ReiPan_PathUserDir(void)
{
    return UserDir().c_str();
}

const char *ReiPan_PathDataDir(void)
{
    return DataDir().c_str();
}

const char *ReiPan_PathAssetsDir(void)
{
    return AssetsDir().c_str();
}

const char *ReiPan_PathDataDirSource(void)
{
    DataDir();
    return g_data_source;
}

int ReiPan_PathLooksLikeDataDir(const char *dir)
{
    if (dir == nullptr || dir[0] == '\0')
    {
        return 0;
    }
    return HoldsGameFiles(dir) ? 1 : 0;
}

int ReiPan_PathHasGameFiles(void)
{
    ReiPan_ConfigLoad();

    const char *env = std::getenv("REIPAN_DATA_DIR");
    if (env != nullptr && env[0] != '\0')
    {
#ifdef REIPAN_SLPS_25544_CHS
        return LooksLikeDataRoot(env) ? 1 : 0;
#else
        return 1;
#endif
    }

    const char *configured = reipan_config.paths.data_folder;
    if (configured[0] != '\0' && HoldsGameFiles(configured))
    {
        return 1;
    }
    return DiscoverDataDir().empty() ? 0 : 1;
}

int ReiPan_PathUseGameFile(const char *path)
{
    if (path == nullptr || !IsFile(path))
    {
        return REIPAN_GAMEFILE_UNREADABLE;
    }

    const std::string file = ForwardSlashes(path);
    const std::string name = file.substr(file.find_last_of('/') + 1);

    std::string setting;
    int         found;
    if (SDL_strcasecmp(name.c_str(), "IMG_BD.BIN") == 0 ||
        SDL_strcasecmp(name.c_str(), "IMG_BD.BIN;1") == 0)
    {
        setting = WithTrailingSlash(ParentOf(file));
        found = FindGameBeside(file, setting);
    }
    else
    {
        setting = file;
        found = FindGameInImage(file, nullptr);
    }
    if (found != REIPAN_GAMEFILE_OK)
    {
        return found;
    }

    /* A write that fails is not the player's problem to solve now: the choice
     * holds for this run, and the config has logged why it was not kept. */
    ReiPan_ConfigSetDataFolder(setting.c_str());
    SDL_Log("ReiPan paths: the player chose %s", setting.c_str());
    return REIPAN_GAMEFILE_OK;
}

void ReiPan_PathLogDataRoot(void)
{
    DataDir();

    char archive[1024];
    if (ReiPan_PathResolveData("\\IMG_BD.BIN;1", archive, sizeof(archive)))
    {
        SDL_Log("ReiPan paths: archive = %s", archive);
    }
    else
    {
        SDL_Log("ReiPan paths: no IMG_BD.BIN (or IMG_BD.BIN;1) in the data root "
                "-- only files of an extracted tree can load");
    }
}

int ReiPan_PathResolveData(const char *ps2_name, char *out, size_t out_size)
{
    const std::string relative = NormalizePs2Path(ps2_name);

    if (relative.empty())
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    const std::string root = DataDir();

    /* Everything on a disc is a file in its root or inside the archive, by
     * sector: there is no tree to look through, and a bin/ that happens to sit
     * beside the image is not one. */
    if (g_playing_from_disc)
    {
        for (const std::string &file : g_disc_files)
        {
            if (SDL_strcasecmp(file.c_str(), relative.c_str()) == 0)
            {
                return Emit(kDiscPrefix + file, out, out_size);
            }
        }
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    /*
     * Three spellings of the same file, all against the one root.  The ROM's
     * table is written relative to the ELF's directory, so every game path
     * arrives as "../bin/data/..." -- dropping that "../" is what lets the data
     * sit in the root itself, which is how every install of this port is laid
     * out.  "bin2" is the second extracted-tree spelling in circulation.
     */
    std::string tried[3];
    int         count = 0;

    tried[count++] = root + relative;

    if (relative.compare(0, 3, "../") == 0)
    {
        tried[count++] = root + relative.substr(3);
    }

    {
        const std::string bare =
            (relative.compare(0, 3, "../") == 0) ? relative.substr(3) : relative;

        if (bare.compare(0, 4, "bin/") == 0)
        {
            tried[count++] = root + "bin2/" + bare.substr(4);
        }
    }

    for (int i = 0; i < count; i++)
    {
        const std::string found = ExistingIsoSpelling(tried[i]);
        if (!found.empty())
        {
            return Emit(found, out, out_size);
        }
    }

    if (out != nullptr && out_size != 0)
    {
        out[0] = '\0';
    }
    return 0;
}

int ReiPan_PathResolveAsset(const char *relative, char *out, size_t out_size)
{
    if (relative == nullptr || relative[0] == '\0')
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    const std::string path = AssetsDir() + relative;

    if (!FileExists(path))
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    return Emit(path, out, out_size);
}

int ReiPan_PathResolveUser(const char *relative, char *out, size_t out_size)
{
    const std::string safe = SanitizeRelative(relative);

    if (safe.empty())
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    return Emit(UserDir() + safe, out, out_size);
}

int ReiPan_PathPrepareUser(const char *relative, char *out, size_t out_size)
{
    char path[1024];

    if (ReiPan_PathResolveUser(relative, path, sizeof(path)) == 0)
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    /* Writers do not create their own directories, and the ROM's debug dumpers
     * name subdirectories that have never existed on the host. */
    const std::string full(path);
    const std::size_t slash = full.find_last_of('/');
    if (slash != std::string::npos)
    {
        const std::string parent = full.substr(0, slash);
        std::error_code   ec;

        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            SDL_Log("ReiPan paths: could not create %s: %s", parent.c_str(),
                    ec.message().c_str());
            if (out != nullptr && out_size != 0)
            {
                out[0] = '\0';
            }
            return 0;
        }
    }

    return Emit(full, out, out_size);
}

}

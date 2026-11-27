#include "reipan_rml_gamefiles.h"
#include "reipan_ui_locale.h"

#include "reipan_rml_common.h"
#include "reipan_rmlui.h"
#include "../io/reipan_input.h"
#include "../io/reipan_paths.h"
#include "../os/reipan_host.h"
#include "../reipan_config.h"
#include "../rendering/reipan_renderer.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <atomic>
#include <mutex>
#include <string>

namespace
{
using ReiPanUiLocale::UiText;

constexpr const char *kDocumentPath = "resources/rml/gamefiles.rml";

/* How long "found" stays up before the fade, and the fade itself, which is
 * gamefiles.rcss's. */
constexpr Uint64 kFoundMs = 1100;
constexpr Uint64 kFadeMs = 450;

/* The game's own frame pacing is not running yet, and a vsync that is off
 * would otherwise let this screen spin a core. */
constexpr Uint64 kTickNs = SDL_NS_PER_SECOND / 60;

constexpr int kActionChoose = 0;
constexpr int kActionQuit = 1;
constexpr int kActions = 2;

enum class Stage
{
    Undecided,  /* nothing has looked yet                 */
    Asking,     /* the screen is up, waiting on the player */
    Found,      /* chosen and saved: saying so, then fading */
    Done,       /* the game can start                     */
};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct ScreenState
{
    Stage stage = Stage::Undecided;
    Rml::ElementDocument *document = nullptr;
    Rml::Element *frame = nullptr;
    Rml::Element *veil = nullptr;
    Rml::Element *actions[kActions] = {};
    Rml::Element *status = nullptr;
    Rml::Element *status_text = nullptr;
    Rml::Element *status_file = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_prompts;
    std::string shown_choose;
    int cursor = kActionChoose;
    Uint64 found_at = 0;
    Uint64 last_tick = 0;
};

/* The dialog in flight.  SDL hands the choice back on a thread of its own, so
 * it waits here for the game thread to take it. */
struct Pick
{
    std::mutex mutex;
    std::string path;           /* guarded by mutex: the file chosen    */
    std::string error;          /* guarded by mutex: why it failed      */
    std::atomic<bool> ready{false};
    std::atomic<bool> open{false};
};

ScreenState g_screen;
Pick g_pick;
Listener g_listener;

void SDLCALL Picked(void *userdata, const char *const *filelist, int filter)
{
    (void)userdata;
    (void)filter;
    /* A null list is an error and an empty one is a cancel. */
    if (filelist == nullptr || filelist[0] != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_pick.mutex);
        g_pick.path = filelist != nullptr ? filelist[0] : "";
        g_pick.error = filelist != nullptr ? "" : SDL_GetError();
        g_pick.ready = true;
    }
    g_pick.open = false;
}

void Quit()
{
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

/* The name the player knows the file by, whichever way the path was written. */
std::string BaseName(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/* "problem", "found" or "" for a plain note. */
void ShowStatus(const char *kind, const std::string &text, const std::string &file)
{
    g_screen.status->SetClass("problem", SDL_strcmp(kind, "problem") == 0);
    g_screen.status->SetClass("found", SDL_strcmp(kind, "found") == 0);
    g_screen.status_text->SetInnerRML(ReiPanRmlCommon::Escape(text));
    g_screen.status_file->SetInnerRML(ReiPanRmlCommon::Escape(file));
}

const char *ProblemText(int result)
{
    switch (result)
    {
    case REIPAN_GAMEFILE_NOT_AN_IMAGE:
        return "That file is not one ReiPan can read. Choose an .iso (a .chd or "
               ".cso has to be turned back into an .iso first) or IMG_BD.BIN.";
    case REIPAN_GAMEFILE_NOT_THE_GAME:
        return "That disc image does not hold Fatal Frame III. Choose an image of "
               "your own European copy of the game.";
    case REIPAN_GAMEFILE_CUT_SHORT:
        return "That copy is cut short: the game's files run past the end of it. "
               "Copy or dump the disc again.";
    case REIPAN_GAMEFILE_OTHER_RELEASE:
#ifdef REIPAN_SLPS_25544_CHS
        return "请选择简体中文版适配目录中的 IMG_BD.BIN。";
#else
        return "That is not a copy of Fatal Frame III that ReiPan can read. Only "
               "the European version works: Project Zero 3, SLES-53825.";
#endif
    case REIPAN_GAMEFILE_NO_TABLE:
#ifdef REIPAN_SLPS_25544_CHS
        return "该目录缺少适配后的 ZERO3.FHD。";
#else
        return "IMG_BD.BIN needs ZERO3.FHD in the same folder. Copy that from "
               "your disc too, then choose IMG_BD.BIN again.";
#endif
    case REIPAN_GAMEFILE_UNREADABLE:
    default:
        return "That file could not be read. Check that it is still there and "
               "not in use, then choose it again.";
    }
}

void OpenDialog()
{
    /* These have to outlive the call: SDL reads them until the dialog answers.
     * "All files" too: dumps are not always named .iso, and a disc copied file
     * by file can leave IMG_BD.BIN;1. */
    static const SDL_DialogFileFilter kFilters[] = {
        {UiText("Game files (*.iso, IMG_BD.BIN)"), "iso;bin"},
        {UiText("All files"), "*"},
    };
    if (g_pick.open.exchange(true))
    {
        return;
    }
    /* OS dialogs belong to the window's thread (reipan_host.h); the call only
     * opens the dialog, so waiting for it costs nothing. */
    ReiPan_HostRunOnMainThread(
        [](void *) {
            SDL_ShowOpenFileDialog(Picked, nullptr, ReiPanRml::GetWindow(), kFilters,
                                   (int)SDL_arraysize(kFilters), nullptr, false);
        },
        nullptr);
}

/* Game thread: what the dialog handed back, if it has. */
void TakePick()
{
    if (!g_pick.ready.exchange(false))
    {
        return;
    }
    std::string path;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(g_pick.mutex);
        path = g_pick.path;
        error = g_pick.error;
    }

    /* Nothing was chosen because nothing could be: a desktop with neither a
     * portal nor zenity, say.  The setting is still there to be edited. */
    if (path.empty())
    {
        SDL_Log("ReiPan: the file dialog failed: %s", error.c_str());
        const char *ini = ReiPan_ConfigPath();
        ShowStatus("problem",
                   "The file dialog could not be opened. Add the path as "
                   "data_folder under [paths] in this file, then start ReiPan "
                   "again:",
                   ini != nullptr ? ini : "reipan.ini");
        return;
    }

    const int result = ReiPan_PathUseGameFile(path.c_str());
    if (result != REIPAN_GAMEFILE_OK)
    {
        ShowStatus("problem", ProblemText(result), BaseName(path));
        return;
    }

    g_screen.stage = Stage::Found;
    g_screen.found_at = SDL_GetTicks();
    g_screen.frame->SetClass("found", true);
    ShowStatus("found", UiText("Found it. Starting the game..."), BaseName(path));
}

void Select(int action)
{
    g_screen.cursor = action;
    for (int i = 0; i < kActions; i++)
    {
        g_screen.actions[i]->SetClass("selected", i == action);
    }
}

void Activate(int action)
{
    if (action == kActionQuit)
    {
        Quit();
    }
    else
    {
        OpenDialog();
    }
}

int ActionOf(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < kActions; i++)
        {
            if (element == g_screen.actions[i])
            {
                return i;
            }
        }
    }
    return -1;
}

bool InElement(Rml::Element *element, const char *id)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element->GetId() == id)
        {
            return true;
        }
    }
    return false;
}

void HandleKey(Rml::Event &event)
{
    switch (event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN))
    {
    case Rml::Input::KI_LEFT:
    case Rml::Input::KI_UP: Select(kActionChoose); break;
    case Rml::Input::KI_RIGHT:
    case Rml::Input::KI_DOWN: Select(kActionQuit); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Activate(g_screen.cursor); break;
    /* Leaving is a choice of its own, as it is on the pause screen. */
    case Rml::Input::KI_ESCAPE: Select(kActionQuit); break;
    default: return;
    }
    event.StopPropagation();
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* The dialog holds the choice while it is open. */
    if (g_screen.stage != Stage::Asking || g_pick.open.load())
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    if (event == Rml::EventId::Click && InElement(target, "prompt-back"))
    {
        Select(kActionQuit);
        return;
    }
    const int action = ActionOf(target);
    if (action < 0)
    {
        return;
    }
    if (event == Rml::EventId::Mouseover)
    {
        if (ReiPanRml::PointerActive())
        {
            Select(action);
        }
    }
    else if (event == Rml::EventId::Click)
    {
        Select(action);
        Activate(action);
    }
}

void RefreshPrompts()
{
    static const ReiPanRmlCommon::Prompt kPrompts[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, UiText("Select"), nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, UiText("Back"), "prompt-back"},
    };
    const std::string prompts = g_screen.stage == Stage::Asking && !g_pick.open.load()
                                    ? ReiPanRmlCommon::PromptsRml(kPrompts, 2)
                                    : std::string();
    if (prompts != g_screen.shown_prompts)
    {
        g_screen.shown_prompts = prompts;
        g_screen.prompts->SetInnerRML(prompts);
    }
}

void RefreshChoosing()
{
    const bool choosing = g_pick.open.load();
    const std::string label = choosing ? UiText("Choosing...") : UiText("Choose Game File");
    if (label != g_screen.shown_choose)
    {
        g_screen.shown_choose = label;
        g_screen.actions[kActionChoose]->SetInnerRML(label);
        g_screen.frame->SetClass("choosing", choosing);
    }
}

bool Open()
{
    Rml::Context *context = ReiPanRml::GetContext();
    if (context == nullptr)
    {
        return false;
    }

    Rml::ElementDocument *document = ReiPanRml::LoadDocument(context, kDocumentPath);
    if (document == nullptr)
    {
        SDL_Log("ReiPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }

    g_screen.frame = document->GetElementById("frame");
    g_screen.veil = document->GetElementById("veil");
    g_screen.actions[kActionChoose] = document->GetElementById("choose");
    g_screen.actions[kActionQuit] = document->GetElementById("quit");
    g_screen.status = document->GetElementById("status");
    g_screen.status_text = document->GetElementById("status-text");
    g_screen.status_file = document->GetElementById("status-file");
    g_screen.prompts = document->GetElementById("prompts");
    if (g_screen.frame == nullptr || g_screen.veil == nullptr ||
        g_screen.actions[kActionChoose] == nullptr ||
        g_screen.actions[kActionQuit] == nullptr || g_screen.status == nullptr ||
        g_screen.status_text == nullptr || g_screen.status_file == nullptr ||
        g_screen.prompts == nullptr)
    {
        SDL_Log("ReiPan RmlUi: %s is missing its elements", kDocumentPath);
        document->Close();
        return false;
    }
    g_screen.document = document;

    for (Rml::EventId id :
         {Rml::EventId::Keydown, Rml::EventId::Click, Rml::EventId::Mouseover})
    {
        document->AddEventListener(id, &g_listener);
    }
    document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);

    /* A setting that has stopped working is worth saying so, or the screen
     * reads as if the player had never chosen. */
    if (reipan_config.paths.data_folder[0] != '\0')
    {
        ShowStatus("", UiText("The game is no longer where the settings say it was:"),
                   reipan_config.paths.data_folder);
    }
    return true;
}

void Close()
{
    g_screen.document->Close();
    g_screen.document = nullptr;
    g_screen.stage = Stage::Done;
}

/* Found: hold, fade, and hand over. */
void AdvanceFound()
{
    const Uint64 elapsed = SDL_GetTicks() - g_screen.found_at;
    if (elapsed >= kFoundMs)
    {
        g_screen.veil->SetClass("closing", true);
    }
    if (elapsed >= kFoundMs + kFadeMs)
    {
        Close();
    }
}

void WaitForNextTick()
{
    const Uint64 now = SDL_GetTicksNS();
    const Uint64 spent = now - g_screen.last_tick;
    if (spent < kTickNs)
    {
        SDL_DelayNS(kTickNs - spent);
    }
    g_screen.last_tick = SDL_GetTicksNS();
}
}

extern "C" {

int ReiPan_RmlGameFilesTick(void)
{
    if (g_screen.stage == Stage::Done)
    {
        return 0;
    }

    if (g_screen.stage == Stage::Undecided)
    {
        if (ReiPan_PathHasGameFiles() != 0)
        {
            g_screen.stage = Stage::Done;
            return 0;
        }
        SDL_Log("ReiPan: no game files found; asking the player for them");
    }

    /* The first of these is what brings the window and RmlUi up, so the screen
     * cannot open ahead of it.  The pad is otherwise opened by the game's own
     * polling, which has not started, and it may be all the player has. */
    ReiPan_RendererBeginFrame();
    ReiPan_InputOpenGamepad();
    if (g_screen.stage == Stage::Undecided)
    {
        if (!Open())
        {
            SDL_Log("ReiPan: the game-files screen is unavailable; starting "
                    "without game files");
            ReiPan_RendererEndFrame();
            g_screen.stage = Stage::Done;
            return 0;
        }
        g_screen.stage = Stage::Asking;
    }

    TakePick();
    if (g_screen.stage == Stage::Found)
    {
        AdvanceFound();
    }
    if (g_screen.stage != Stage::Done)
    {
        RefreshPrompts();
        RefreshChoosing();
    }
    ReiPan_RendererEndFrame();
    WaitForNextTick();
    return g_screen.stage == Stage::Done ? 0 : 1;
}

int ReiPan_RmlGameFilesAsking(void)
{
    return g_screen.stage != Stage::Done ? 1 : 0;
}
}

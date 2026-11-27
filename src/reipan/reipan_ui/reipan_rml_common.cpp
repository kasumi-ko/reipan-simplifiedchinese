#include "reipan_rml_common.h"
#include "reipan_rmlui.h"
#include "reipan_ui_locale.h"

#include "ingame/sound/snd_ctrl.h"      // SndSePlay

#include <RmlUi/Core.h>

namespace
{
/* The system SEs the game's menus play through CheckPadTrgPlaySe and
 * MenuCsrIsMovedSe (outgame/option_top.c), at their level and pitch. */
constexpr unsigned int kSeCursor = 0x1a9b0000;
constexpr unsigned int kSeCancel = 0x1a9b0001;
constexpr unsigned int kSeDecide = 0x1a9b0003;

void PlaySe(unsigned int se)
{
    SndSePlay(se, 100, 0x1000, 0, nullptr, 0);
}
}

namespace ReiPanRmlCommon
{
void PlayCursor()
{
    PlaySe(kSeCursor);
}

void PlayDecide()
{
    PlaySe(kSeDecide);
}

void PlayCancel()
{
    PlaySe(kSeCancel);
}

std::string Escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string PromptsRml(const Prompt *prompts, int count)
{
    const bool pad = ReiPanRml::UsingPad();
    std::string rml;
    for (int i = 0; i < count; i++)
    {
        const Prompt &prompt = prompts[i];
        rml += "<span class=\"prompt\"";
        if (prompt.id != nullptr)
        {
            rml += " id=\"";
            rml += prompt.id;
            rml += "\"";
        }
        std::string key = pad ? ReiPanRml::PadButtonName(prompt.pad) : prompt.key;
        if (pad && prompt.pad_text != nullptr)
        {
            key = prompt.pad_text;
        }
        else if (pad && prompt.pad_pair != SDL_GAMEPAD_BUTTON_INVALID)
        {
            key += " / ";
            key += ReiPanRml::PadButtonName(prompt.pad_pair);
        }
        rml += "><span class=\"key\">";
        rml += Escape(ReiPanUiLocale::Translate(key));
        rml += "</span>";
        rml += Escape(ReiPanUiLocale::Translate(prompt.action));
        rml += "</span>";
    }
    return rml;
}

void ConfirmDialog::Attach(Rml::ElementDocument *document, const char *id)
{
    Detach();
    if (document == nullptr)
    {
        return;
    }
    root_ = document->GetElementById(id);
    const std::string base = id;
    yes_ = document->GetElementById(base + "-yes");
    no_ = document->GetElementById(base + "-no");
    if (root_ == nullptr || yes_ == nullptr || no_ == nullptr)
    {
        root_ = yes_ = no_ = nullptr;
        return;
    }
    Rml::ElementList found;
    root_->GetElementsByClassName(found, "confirm-text");
    text_ = found.empty() ? nullptr : found[0];
    found.clear();
    root_->GetElementsByClassName(found, "confirm-note");
    note_ = found.empty() ? nullptr : found[0];

    for (Rml::Element *choice : {yes_, no_})
    {
        choice->AddEventListener(Rml::EventId::Click, this);
        choice->AddEventListener(Rml::EventId::Mouseover, this);
    }
}

void ConfirmDialog::Detach()
{
    for (Rml::Element *choice : {yes_, no_})
    {
        if (choice != nullptr)
        {
            choice->RemoveEventListener(Rml::EventId::Click, this);
            choice->RemoveEventListener(Rml::EventId::Mouseover, this);
        }
    }
    root_ = text_ = note_ = yes_ = no_ = nullptr;
    on_yes_ = nullptr;
    on_no_ = nullptr;
}

void ConfirmDialog::Open(const std::string &text, const std::string &note,
                         std::function<void()> on_yes, std::function<void()> on_no)
{
    if (root_ == nullptr)
    {
        return;
    }
    if (text_ != nullptr)
    {
        text_->SetInnerRML(Escape(text));
    }
    if (note_ != nullptr)
    {
        note_->SetInnerRML(Escape(note));
        note_->SetClass("hidden", note.empty());
    }
    on_yes_ = std::move(on_yes);
    on_no_ = std::move(on_no);
    Select(false);
    root_->SetClass("hidden", false);
}

void ConfirmDialog::Close()
{
    if (root_ != nullptr)
    {
        root_->SetClass("hidden", true);
    }
    on_yes_ = nullptr;
    on_no_ = nullptr;
}

bool ConfirmDialog::IsOpen() const
{
    return root_ != nullptr && !root_->IsClassSet("hidden");
}

bool ConfirmDialog::HandleKey(int key)
{
    if (!IsOpen())
    {
        return false;
    }
    switch (key)
    {
    case Rml::Input::KI_LEFT:
    case Rml::Input::KI_RIGHT:
        Select(!yes_selected_);
        PlayCursor();
        break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE:
        Answer(yes_selected_);
        break;
    case Rml::Input::KI_ESCAPE:
        Answer(false);
        break;
    default:
        break;
    }
    return true;
}

void ConfirmDialog::ProcessEvent(Rml::Event &event)
{
    if (!IsOpen())
    {
        return;
    }
    const bool on_yes = event.GetCurrentElement() == yes_;
    if (event == Rml::EventId::Mouseover)
    {
        if (ReiPanRml::PointerActive() && on_yes != yes_selected_)
        {
            Select(on_yes);
        }
    }
    else if (event == Rml::EventId::Click)
    {
        Answer(on_yes);
    }
}

void ConfirmDialog::Select(bool yes)
{
    yes_selected_ = yes;
    yes_->SetClass("selected", yes);
    no_->SetClass("selected", !yes);
}

void ConfirmDialog::Answer(bool yes)
{
    std::function<void()> on_yes = std::move(on_yes_);
    std::function<void()> on_no = std::move(on_no_);
    Close();
    if (yes)
    {
        PlayDecide();
        if (on_yes)
        {
            on_yes();
        }
    }
    else
    {
        PlayCancel();
        if (on_no)
        {
            on_no();
        }
    }
}
}

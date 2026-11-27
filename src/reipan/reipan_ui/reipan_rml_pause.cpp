#include "reipan_rml_pause.h"
#include "reipan_rml_common.h"
#include "reipan_rml_options.h"
#include "reipan_rmlui.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/pause.rml";

/* The rows, in the order and with the codes of the layout menu's choice. */
enum Item
{
    ITEM_CONTINUE = REIPAN_RML_PAUSE_CONTINUE,
    ITEM_OPTIONS = REIPAN_RML_PAUSE_OPTIONS,
    ITEM_QUIT = REIPAN_RML_PAUSE_QUIT,
    ITEM_COUNT,
};

constexpr const char *kItemIds[ITEM_COUNT] = {"item-continue", "item-options",
                                              "item-quit"};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct PauseState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *items[ITEM_COUNT] = {};
    Rml::Element *prompts = nullptr;
    std::string shown_prompts;
    bool open = false;
    /* Options is up over the pause screen, which waits underneath. */
    bool in_options = false;
    int cursor = ITEM_CONTINUE;
    int result = REIPAN_RML_PAUSE_NONE;
    ReiPanRmlCommon::ConfirmDialog confirm;
};

PauseState g_pause;
Listener g_listener;

void Select(int item)
{
    g_pause.cursor = item;
    for (int i = 0; i < ITEM_COUNT; i++)
    {
        if (g_pause.items[i] != nullptr)
        {
            g_pause.items[i]->SetClass("selected", i == item);
        }
    }
}

void ShowDocument()
{
    g_pause.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
}

void Finish(int result)
{
    g_pause.result = result;
}

void Choose(int item)
{
    switch (item)
    {
    case ITEM_CONTINUE:
        ReiPanRmlCommon::PlayDecide();
        Finish(REIPAN_RML_PAUSE_CONTINUE);
        break;
    case ITEM_OPTIONS:
        /* The options screen opens over this one, which comes back when the
         * player backs out of it; the game's own option phase only when the
         * screen cannot open. */
        if (ReiPan_RmlOptionsOpen(REIPAN_RML_OPTIONS_FROM_PAUSE) != 0)
        {
            g_pause.in_options = true;
            g_pause.document->Hide();
        }
        else
        {
            ReiPanRmlCommon::PlayDecide();
            Finish(REIPAN_RML_PAUSE_OPTIONS);
        }
        break;
    case ITEM_QUIT:
        ReiPanRmlCommon::PlayDecide();
        g_pause.confirm.Open("Quit the game?",
                             "Anything since your last save will be lost.",
                             [] { Finish(REIPAN_RML_PAUSE_QUIT); });
        break;
    default:
        break;
    }
}

/* The cursor goes round. */
void Move(int direction)
{
    Select((g_pause.cursor + direction + ITEM_COUNT) % ITEM_COUNT);
    ReiPanRmlCommon::PlayCursor();
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_pause.confirm.HandleKey(key))
    {
        event.StopPropagation();
        return;
    }
    switch (key)
    {
    case Rml::Input::KI_UP: Move(-1); break;
    case Rml::Input::KI_DOWN: Move(1); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Choose(g_pause.cursor); break;
    /* SELECT, the button that paused, resumes as it does on the layout
     * menu; Escape, and Triangle, Circle and START with it, back out. */
    case Rml::Input::KI_TAB:
    case Rml::Input::KI_ESCAPE:
        ReiPanRmlCommon::PlayCancel();
        Finish(REIPAN_RML_PAUSE_CONTINUE);
        break;
    default: return;
    }
    event.StopPropagation();
}

int ItemAt(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < ITEM_COUNT; i++)
        {
            if (element == g_pause.items[i])
            {
                return i;
            }
        }
    }
    return -1;
}

void Listener::ProcessEvent(Rml::Event &event)
{
    if (!g_pause.open || g_pause.in_options ||
        g_pause.result != REIPAN_RML_PAUSE_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_pause.confirm.IsOpen())
    {
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    const int item = ItemAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (ReiPanRml::PointerActive() && item >= 0 && item != g_pause.cursor)
        {
            Select(item);
        }
    }
    else if (event == Rml::EventId::Click)
    {
        if (item >= 0)
        {
            Choose(item);
            return;
        }
        for (Rml::Element *e = target; e != nullptr; e = e->GetParentNode())
        {
            if (e->GetId() == "prompt-resume")
            {
                ReiPanRmlCommon::PlayCancel();
                Finish(REIPAN_RML_PAUSE_CONTINUE);
                return;
            }
        }
    }
}

void RefreshPrompts()
{
    static const ReiPanRmlCommon::Prompt kPrompts[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_BACK, "Resume", "prompt-resume"},
    };
    const std::string prompts = ReiPanRmlCommon::PromptsRml(kPrompts, 2);
    if (prompts != g_pause.shown_prompts && g_pause.prompts != nullptr)
    {
        g_pause.prompts->SetInnerRML(prompts);
        g_pause.shown_prompts = prompts;
    }
}
}

namespace ReiPanRmlPause
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_pause.document = ReiPanRml::LoadDocument(context, kDocumentPath);
    if (g_pause.document == nullptr)
    {
        SDL_Log("ReiPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    for (int i = 0; i < ITEM_COUNT; i++)
    {
        g_pause.items[i] = g_pause.document->GetElementById(kItemIds[i]);
    }
    g_pause.prompts = g_pause.document->GetElementById("prompts");
    g_pause.confirm.Attach(g_pause.document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        g_pause.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_pause.confirm.Detach();
    g_pause = PauseState();
}

void Frame()
{
    if (!g_pause.open)
    {
        return;
    }
    /* Back from Options: the pause screen comes up again where it was. */
    if (g_pause.in_options && ReiPan_RmlOptionsIsOpen() == 0)
    {
        g_pause.in_options = false;
        ShowDocument();
    }
    if (!g_pause.in_options)
    {
        RefreshPrompts();
    }
}
}

extern "C" {

int ReiPan_RmlPauseOpen(int cursor)
{
    if (g_pause.document == nullptr)
    {
        return 0;
    }
    g_pause.open = true;
    g_pause.in_options = false;
    g_pause.result = REIPAN_RML_PAUSE_NONE;
    g_pause.shown_prompts.clear();
    g_pause.confirm.Close();
    Select(cursor >= 0 && cursor < ITEM_COUNT ? cursor : ITEM_CONTINUE);
    ShowDocument();
    RefreshPrompts();
    return 1;
}

void ReiPan_RmlPauseClose(void)
{
    if (g_pause.in_options)
    {
        ReiPan_RmlOptionsClose();
    }
    if (g_pause.document != nullptr)
    {
        g_pause.confirm.Close();
        g_pause.document->Hide();
    }
    g_pause.open = false;
    g_pause.in_options = false;
}

int ReiPan_RmlPauseIsOpen(void)
{
    return g_pause.open ? 1 : 0;
}

int ReiPan_RmlPauseConsumeResult(void)
{
    const int result = g_pause.result;
    g_pause.result = REIPAN_RML_PAUSE_NONE;
    return result;
}
}

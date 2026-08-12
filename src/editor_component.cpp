#include "editor_component.h"
#include "plugin_identity.h"

#include <cmath>

namespace resonance
{
namespace
{
const auto background = juce::Colour::fromRGB (10, 15, 24);
const auto card = juce::Colour::fromRGB (18, 27, 41);
const auto cardEdge = juce::Colour::fromRGB (42, 57, 76);
const auto primary = juce::Colour::fromRGB (93, 214, 196);
const auto secondary = juce::Colour::fromRGB (123, 151, 255);
const auto textMain = juce::Colour::fromRGB (235, 241, 248);
const auto textMuted = juce::Colour::fromRGB (137, 153, 173);
const auto warning = juce::Colour::fromRGB (247, 184, 88);
const auto danger = juce::Colour::fromRGB (245, 103, 119);
constexpr std::int64_t editorVelocityVariationSeed = 18421;
constexpr int editorVelocityVariationMaximumDelta = 8;
constexpr int velocityScopeWholeLoop = 1;
constexpr int velocityScopeSelectedNote = 2;
constexpr std::int64_t maximumVelocityVariationSeed = 2147483647;
constexpr int maximumVelocityVariationDelta = 32;
constexpr juce::int64 maximumEditCommandBytes = 256 * 1024;
constexpr std::size_t maximumEditCommandChanges = 128;

juce::Font uiFont (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions ("Segoe UI", height, style));
}

juce::String formatDeviceSummary (const juce::AudioDeviceManager& manager)
{
    auto* device = manager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No output device is open. Choose one below.";

    return device->getTypeName() + "  /  " + device->getName()
           + "\n" + juce::String (device->getCurrentSampleRate(), 0) + " Hz  /  "
           + juce::String (device->getCurrentBufferSizeSamples()) + " samples  /  "
           + juce::String (device->getOutputLatencyInSamples()) + " samples latency";
}

int snapComboId (double snap)
{
    if (std::abs (snap - 0.125) < 1.0e-8) return 1;
    if (std::abs (snap - 0.25) < 1.0e-8)  return 2;
    if (std::abs (snap - 0.5) < 1.0e-8)   return 3;
    return 4;
}

double snapForComboId (int id)
{
    switch (id)
    {
        case 1:  return 0.125;
        case 2:  return 0.25;
        case 3:  return 0.5;
        default: return 1.0;
    }
}

int loopComboId (double beats)
{
    if (beats <= 4.0)  return 1;
    if (beats <= 8.0)  return 2;
    if (beats <= 16.0) return 3;
    return 4;
}

double loopForComboId (int id)
{
    switch (id)
    {
        case 1:  return 4.0;
        case 2:  return 8.0;
        case 3:  return 16.0;
        default: return 32.0;
    }
}

juce::String shortStateHash (const juce::String& hash)
{
    return hash.substring (0, 8).toUpperCase();
}

juce::String noteName (int midiNote)
{
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
}
} // namespace

class MainEditorComponent::PluginEditorWindow final : public juce::DocumentWindow
{
private:
    class AuditionContent final : public juce::Component,
                                  private juce::Timer
    {
    public:
        AuditionContent (std::unique_ptr<juce::AudioProcessorEditor> editor,
                         RealtimeEngine& realtimeEngine,
                         juce::MidiKeyboardState& sharedKeyboardState)
            : engine (realtimeEngine),
              pluginEditor (std::move (editor)),
              keyboard (sharedKeyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
        {
            setOpaque (true);
            playButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.55f));
            panicButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.55f));

            playButton.onClick = [this] { engine.setPlaying (! engine.isPlaying()); };
            stopButton.onClick = [this] { engine.stopAndRewind(); };
            panicButton.onClick = [this] { engine.panic(); };

            statusLabel.setFont (uiFont (11.0f, juce::Font::bold));
            statusLabel.setJustificationType (juce::Justification::centredRight);
            statusLabel.setColour (juce::Label::textColourId, primary);

            keyboard.setAvailableRange (36, 108);
            keyboard.setLowestVisibleKey (36);
            keyboard.setKeyWidth (25.0f);
            keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId,
                                juce::Colour::fromRGB (224, 232, 240));
            keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId,
                                juce::Colour::fromRGB (19, 27, 39));
            keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, primary);

            for (auto* button : { &playButton, &stopButton, &panicButton })
                addAndMakeVisible (*button);

            addAndMakeVisible (statusLabel);
            addAndMakeVisible (keyboard);
            addAndMakeVisible (*pluginEditor);

            const auto editorWidth = juce::jmax (720, pluginEditor->getWidth());
            const auto editorHeight = juce::jmax (500, pluginEditor->getHeight());
            setSize (editorWidth, editorHeight + auditionHeight);
            updateAuditionStatus();
            startTimerHz (15);
        }

        ~AuditionContent() override
        {
            stopTimer();
            engine.panic();
        }

        void paint (juce::Graphics& graphics) override
        {
            graphics.fillAll (background);
            auto auditionArea = getLocalBounds().removeFromTop (auditionHeight).toFloat();
            juce::ColourGradient gradient (card, auditionArea.getTopLeft(),
                                           background, auditionArea.getBottomLeft(), false);
            graphics.setGradientFill (gradient);
            graphics.fillRect (auditionArea);
            graphics.setColour (cardEdge);
            graphics.drawHorizontalLine (auditionHeight - 1, 0.0f, static_cast<float> (getWidth()));
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto auditionArea = area.removeFromTop (auditionHeight).reduced (10, 6);
            auto controls = auditionArea.removeFromTop (30);
            playButton.setBounds (controls.removeFromLeft (112));
            controls.removeFromLeft (7);
            stopButton.setBounds (controls.removeFromLeft (72));
            controls.removeFromLeft (7);
            panicButton.setBounds (controls.removeFromLeft (72));
            controls.removeFromLeft (12);
            statusLabel.setBounds (controls);
            auditionArea.removeFromTop (5);
            keyboard.setBounds (auditionArea);
            pluginEditor->setBounds (area);
        }

    private:
        void timerCallback() override { updateAuditionStatus(); }

        void updateAuditionStatus()
        {
            const auto isPlaying = engine.isPlaying();
            playButton.setButtonText (isPlaying ? "Pause loop" : "Play loop");
            playButton.setEnabled (engine.isPrepared());
            statusLabel.setText (isPlaying
                                     ? "LOOP PLAYING - EDIT SURGE, THEN CAPTURE B IN RESONANCE"
                                     : "AUDITION READY - EDIT SURGE, THEN CAPTURE B IN RESONANCE",
                                  juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, isPlaying ? primary : textMuted);
        }

        static constexpr int auditionHeight = 86;
        RealtimeEngine& engine;
        std::unique_ptr<juce::AudioProcessorEditor> pluginEditor;
        juce::MidiKeyboardComponent keyboard;
        juce::TextButton playButton { "Play loop" };
        juce::TextButton stopButton { "Stop" };
        juce::TextButton panicButton { "Panic" };
        juce::Label statusLabel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuditionContent)
    };

public:
    PluginEditorWindow (std::unique_ptr<juce::AudioProcessorEditor> editor,
                        RealtimeEngine& engine,
                        juce::MidiKeyboardState& keyboardState)
        : juce::DocumentWindow ("Surge XT - Resonance track",
                                background,
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        auto* auditionContent = new AuditionContent (std::move (editor), engine, keyboardState);
        const auto contentWidth = auditionContent->getWidth();
        const auto contentHeight = auditionContent->getHeight();
        setContentOwned (auditionContent, true);
        centreWithSize (juce::jmax (720, getContentComponent()->getWidth()),
                        juce::jmax (586, getContentComponent()->getHeight()));
        setResizeLimits (720, 586,
                         juce::jmax (1800, contentWidth),
                         juce::jmax (1200, contentHeight));
    }

    void closeButtonPressed() override { setVisible (false); }
};

MainEditorComponent::MainEditorComponent (juce::File inventoryFile,
                                          juce::File quarantineFile,
                                          juce::PropertiesFile* settings)
    : inventoryPath (std::move (inventoryFile)),
      quarantinePath (std::move (quarantineFile)),
      settingsFile (settings)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setLookAndFeel (&lookAndFeel);
    // The shelf is user data, so it sits beside the settings file rather than in the
    // repository or a project folder.
    soundShelfPath = settingsFile != nullptr
                         ? settingsFile->getFile().getSiblingFile ("sound-shelf.json")
                         : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                               .getChildFile ("ResonanceMusicEditor")
                               .getChildFile ("sound-shelf.json");
    configureControls();
    initialiseAudioAndPlugin();
    // A corrupt shelf must not stop the editor from starting; it starts empty and says so.
    const auto shelfLoaded = soundShelf.loadFrom (soundShelfPath);
    if (shelfLoaded.failed())
    {
        soundShelf.clear();
        projectStatusMessage = "SOUND SHELF IGNORED  /  " + shelfLoaded.getErrorMessage().toUpperCase();
    }
    projectChanged();
    startTimerHz (30);
}

MainEditorComponent::~MainEditorComponent()
{
    stopTimer();
    activeFileChooser.reset();
    pluginEditorWindow.reset();
    project.setChangeCallback ({});
    saveSettings();

    if (keyboardListenerRegistered)
        keyboardState.removeListener (&engine.getMidiCollector());
    if (midiCallbackRegistered)
        deviceManager.removeMidiInputDeviceCallback ({}, &engine.getMidiCollector());
    if (audioCallbackRegistered)
        deviceManager.removeAudioCallback (&engine);

    engine.shutdown();
    deviceManager.closeAudioDevice();
    setLookAndFeel (nullptr);
}

void MainEditorComponent::configureControls()
{
    lookAndFeel.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (31, 44, 62));
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, primary.darker (0.25f));
    lookAndFeel.setColour (juce::TextButton::textColourOffId, textMain);
    lookAndFeel.setColour (juce::TextButton::textColourOnId, background);
    lookAndFeel.setColour (juce::Slider::trackColourId, primary.withAlpha (0.85f));
    lookAndFeel.setColour (juce::Slider::thumbColourId, textMain);
    lookAndFeel.setColour (juce::Slider::backgroundColourId, cardEdge.withAlpha (0.65f));
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, textMain);
    lookAndFeel.setColour (juce::Slider::textBoxBackgroundColourId, background.withAlpha (0.6f));
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, background.withAlpha (0.7f));
    lookAndFeel.setColour (juce::ComboBox::textColourId, textMain);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, cardEdge);
    lookAndFeel.setColour (juce::Label::textColourId, textMain);

    titleLabel.setText ("RESONANCE", juce::dontSendNotification);
    titleLabel.setFont (uiFont (25.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, textMain);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("EDITABLE VST3 SONG  /  A-B SOUND WORKFLOW", juce::dontSendNotification);
    subtitleLabel.setFont (uiFont (10.5f, juce::Font::bold));
    subtitleLabel.setColour (juce::Label::textColourId, textMuted);
    addAndMakeVisible (subtitleLabel);

    projectNameLabel.setFont (uiFont (14.0f, juce::Font::bold));
    projectNameLabel.setColour (juce::Label::textColourId, primary);
    addAndMakeVisible (projectNameLabel);

    statusLabel.setFont (uiFont (11.0f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    trackNameLabel.setFont (uiFont (19.0f, juce::Font::bold));
    trackNameLabel.setText ("01  /  Surge instrument", juce::dontSendNotification);
    addAndMakeVisible (trackNameLabel);

    trackMetaLabel.setFont (uiFont (12.0f));
    trackMetaLabel.setColour (juce::Label::textColourId, textMuted);
    addAndMakeVisible (trackMetaLabel);

    soundWorkflowLabel.setFont (uiFont (11.0f, juce::Font::bold));
    soundWorkflowLabel.setColour (juce::Label::textColourId, textMuted);
    soundWorkflowLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (soundWorkflowLabel);

    transportPositionLabel.setFont (uiFont (14.0f, juce::Font::bold));
    transportPositionLabel.setColour (juce::Label::textColourId, primary);
    transportPositionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (transportPositionLabel);

    deviceSummaryLabel.setFont (uiFont (12.0f));
    deviceSummaryLabel.setColour (juce::Label::textColourId, textMuted);
    deviceSummaryLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (deviceSummaryLabel);

    diagnosticLabel.setFont (uiFont (11.0f, juce::Font::bold));
    diagnosticLabel.setColour (juce::Label::textColourId, textMuted);
    diagnosticLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (diagnosticLabel);

    editProposalSummaryLabel.setFont (uiFont (12.0f, juce::Font::bold));
    editProposalSummaryLabel.setColour (juce::Label::textColourId, textMain);
    editProposalSummaryLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (editProposalSummaryLabel);

    editProposalDiffLabel.setFont (uiFont (10.5f));
    editProposalDiffLabel.setColour (juce::Label::textColourId, textMuted);
    editProposalDiffLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (editProposalDiffLabel);

    dynamicsScopeLabel.setText ("TARGET", juce::dontSendNotification);
    dynamicsStrengthLabel.setText ("MAX +/-", juce::dontSendNotification);
    dynamicsSeedLabel.setText ("SEED", juce::dontSendNotification);
    for (auto* label : { &dynamicsScopeLabel, &dynamicsStrengthLabel, &dynamicsSeedLabel })
    {
        label->setFont (uiFont (9.0f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, textMuted);
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*label);
    }

    dynamicsScopeCombo.addItem ("Whole loop", velocityScopeWholeLoop);
    dynamicsScopeCombo.addItem ("Selected notes", velocityScopeSelectedNote);
    dynamicsScopeCombo.setSelectedId (velocityScopeWholeLoop, juce::dontSendNotification);
    dynamicsScopeCombo.setTooltip ("Choose whether dynamics targets every loop note or only the selected note");
    dynamicsScopeCombo.onChange = [this] { refreshEditPreviewControls(); };
    addAndMakeVisible (dynamicsScopeCombo);

    dynamicsStrengthEditor.setInputRestrictions (2, "0123456789");
    dynamicsStrengthEditor.setText (juce::String (editorVelocityVariationMaximumDelta), false);
    dynamicsStrengthEditor.setSelectAllWhenFocused (true);
    dynamicsStrengthEditor.setJustification (juce::Justification::centred);
    dynamicsStrengthEditor.setTooltip ("Maximum velocity change from 1 through 32");
    dynamicsStrengthEditor.onTextChange = [this] { refreshEditPreviewControls(); };

    dynamicsSeedEditor.setInputRestrictions (10, "0123456789");
    dynamicsSeedEditor.setText (juce::String::formatted ("%lld",
                                                        static_cast<long long> (
                                                            editorVelocityVariationSeed)),
                                false);
    dynamicsSeedEditor.setSelectAllWhenFocused (true);
    dynamicsSeedEditor.setJustification (juce::Justification::centred);
    dynamicsSeedEditor.setTooltip ("Deterministic seed from 0 through 2147483647");
    dynamicsSeedEditor.onTextChange = [this] { refreshEditPreviewControls(); };

    for (auto* editor : { &dynamicsStrengthEditor, &dynamicsSeedEditor })
    {
        editor->setColour (juce::TextEditor::backgroundColourId, background.withAlpha (0.75f));
        editor->setColour (juce::TextEditor::textColourId, textMain);
        editor->setColour (juce::TextEditor::outlineColourId, cardEdge);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, secondary);
        addAndMakeVisible (*editor);
    }

    for (auto* button : { &newButton, &openButton, &saveButton, &undoButton, &redoButton,
                          &playButton, &stopButton, &panicButton, &pluginEditorButton,
                          &auditionProjectSoundButton, &captureSoundButton,
                          &auditionCandidateButton, &applySoundButton, &rejectSoundButton,
                          &previewSelectedEditButton, &previewDynamicsButton,
                          &loadCommandButton, &copyHashButton,
                          &loadShelfButton, &saveShelfButton, &removeShelfButton,
                          &auditionEditProjectButton,
                          &auditionEditCandidateButton, &applyEditButton, &rejectEditButton,
                          &addTrackButton, &removeTrackButton,
                          &moveTrackLeftButton, &moveTrackRightButton })
        addAndMakeVisible (*button);

    addAndMakeVisible (trackMuteButton);
    addAndMakeVisible (trackSoloButton);

    playButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.55f));
    panicButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.55f));
    saveButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    captureSoundButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    applySoundButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.55f));
    rejectSoundButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.65f));
    previewSelectedEditButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    previewDynamicsButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.45f));
    loadCommandButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.45f));
    copyHashButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.65f));
    auditionEditCandidateButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    applyEditButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.55f));
    rejectEditButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.65f));
    addTrackButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.62f));
    removeTrackButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.72f));

    newButton.onClick = [this] { confirmDiscardIfNeeded ([this] { startNewProject(); }); };
    openButton.onClick = [this] { confirmDiscardIfNeeded ([this] { chooseProjectToOpen(); }); };
    saveButton.onClick = [this] { saveProject(); };
    undoButton.onClick = [this] { performUndoRedo (false); };
    redoButton.onClick = [this] { performUndoRedo (true); };
    playButton.onClick = [this]
    {
        engine.setPlaying (! engine.isPlaying());
        updateStatus();
    };
    stopButton.onClick = [this] { engine.stopAndRewind(); };
    panicButton.onClick = [this] { engine.panic(); };
    pluginEditorButton.onClick = [this] { openPluginEditor(); };
    auditionProjectSoundButton.onClick = [this] { auditionProjectSound(); };
    captureSoundButton.onClick = [this] { captureSoundCandidate(); };
    auditionCandidateButton.onClick = [this] { auditionSoundCandidate(); };
    applySoundButton.onClick = [this] { applySoundCandidate(); };
    rejectSoundButton.onClick = [this] { rejectSoundCandidate(); };
    previewSelectedEditButton.onClick = [this] { previewSelectedNoteEdit(); };
    previewDynamicsButton.onClick = [this] { previewVelocityVariation(); };
    loadCommandButton.onClick = [this] { chooseEditCommandFile(); };
    copyHashButton.onClick = [this] { copyProjectContentHash(); };
    auditionEditProjectButton.onClick = [this] { auditionEditProject(); };
    auditionEditCandidateButton.onClick = [this] { auditionEditCandidate(); };
    applyEditButton.onClick = [this] { applyEditPreview(); };
    rejectEditButton.onClick = [this] { rejectEditPreview(); };
    addTrackButton.onClick = [this] { addInstrumentTrack(); };
    removeTrackButton.onClick = [this] { removeActiveTrack(); };
    moveTrackLeftButton.onClick = [this] { moveActiveTrack (-1); };
    moveTrackRightButton.onClick = [this] { moveActiveTrack (1); };

    trackSelector.setTooltip ("Choose the instrument track shown in the piano roll and Surge editor");
    trackSelector.onChange = [this]
    {
        if (! refreshingProjectControls)
            selectTrack (trackSelector.getSelectedId() - 1);
    };
    addAndMakeVisible (trackSelector);

    previewSelectedEditButton.setTooltip ("Preview the selected note one semitone higher");
    previewDynamicsButton.setTooltip ("Resolve the target, maximum velocity change, and seed into candidate B");
    loadShelfButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    saveShelfButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.65f));
    removeShelfButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.72f));
    loadShelfButton.onClick = [this] { loadSoundFromShelf(); };
    saveShelfButton.onClick = [this] { saveSoundToShelf(); };
    removeShelfButton.onClick = [this] { removeSoundFromShelf(); };
    loadShelfButton.setTooltip ("Load the chosen shelf sound as candidate B without changing the accepted sound");
    saveShelfButton.setTooltip ("Save candidate B, or the accepted sound when no candidate is pending, to the shelf");
    removeShelfButton.setTooltip ("Delete the chosen sound from the shelf");
    shelfLabel.setText ("SHELF", juce::dontSendNotification);
    shelfLabel.setFont (uiFont (10.0f, juce::Font::bold));
    shelfLabel.setColour (juce::Label::textColourId, textMuted);
    addAndMakeVisible (shelfLabel);
    shelfCombo.setTextWhenNoChoicesAvailable ("No saved sounds");
    shelfCombo.setTextWhenNothingSelected ("Choose a saved sound");
    shelfCombo.onChange = [this]
    {
        if (! refreshingProjectControls)
            refreshShelfControls();
    };
    addAndMakeVisible (shelfCombo);

    loadCommandButton.setTooltip ("Preview a version-1 edit-command file as candidate B without applying it");
    copyHashButton.setTooltip ("Copy this project's content SHA-256, track id, and clip id for authoring a command");

    soundNameEditor.setTextToShowWhenEmpty ("Candidate sound name", textMuted);
    soundNameEditor.setInputRestrictions (80);
    soundNameEditor.setText ("Captured sound", false);
    soundNameEditor.setSelectAllWhenFocused (true);
    soundNameEditor.onTextChange = [this]
    {
        if (! soundCandidate.has_value())
            return;

        const auto edited = soundNameEditor.getText().trim().substring (0, 80);
        if (edited.isNotEmpty())
            soundCandidate->name = edited;
        refreshSoundControls();
    };
    soundNameEditor.setColour (juce::TextEditor::backgroundColourId, background.withAlpha (0.75f));
    soundNameEditor.setColour (juce::TextEditor::textColourId, textMain);
    soundNameEditor.setColour (juce::TextEditor::outlineColourId, cardEdge);
    soundNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, secondary);
    addAndMakeVisible (soundNameEditor);

    bpmLabel.setText ("BPM", juce::dontSendNotification);
    gainLabel.setText ("MASTER", juce::dontSendNotification);
    snapLabel.setText ("SNAP", juce::dontSendNotification);
    loopLengthLabel.setText ("LOOP", juce::dontSendNotification);
    velocityLabel.setText ("VELOCITY", juce::dontSendNotification);
    trackGainLabel.setText ("GAIN", juce::dontSendNotification);
    trackPanLabel.setText ("PAN", juce::dontSendNotification);
    for (auto* label : { &bpmLabel, &gainLabel, &snapLabel, &loopLengthLabel, &velocityLabel,
                         &trackGainLabel, &trackPanLabel })
    {
        label->setFont (uiFont (11.0f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, textMuted);
        label->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*label);
    }

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 24);
    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (project.getTempoBpm(), juce::dontSendNotification);
    bpmSlider.onDragStart = [this]
    {
        bpmGestureActive = true;
        project.beginUndoTransaction ("Change tempo");
    };
    bpmSlider.onDragEnd = [this] { bpmGestureActive = false; };
    bpmSlider.onValueChange = [this]
    {
        if (! refreshingProjectControls)
        {
            if (! bpmGestureActive)
                project.beginUndoTransaction ("Change tempo");
            project.setTempoBpm (bpmSlider.getValue());
        }
    };
    addAndMakeVisible (bpmSlider);

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
    gainSlider.setRange (-30.0, 0.0, 0.5);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setValue (settingsFile != nullptr ? settingsFile->getDoubleValue ("masterGainDb", -12.0) : -12.0,
                         juce::dontSendNotification);
    gainSlider.onValueChange = [this]
    {
        engine.setMasterGainDecibels (static_cast<float> (gainSlider.getValue()));
    };
    addAndMakeVisible (gainSlider);
    engine.setMasterGainDecibels (static_cast<float> (gainSlider.getValue()));

    trackGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    trackGainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 24);
    trackGainSlider.setRange (-60.0, 12.0, 0.5);
    trackGainSlider.setTextValueSuffix (" dB");
    trackGainSlider.onDragStart = [this]
    {
        trackGainGestureActive = true;
        project.beginUndoTransaction ("Change track gain");
    };
    trackGainSlider.onDragEnd = [this] { trackGainGestureActive = false; };
    trackGainSlider.onValueChange = [this]
    {
        if (refreshingProjectControls)
            return;

        if (! trackGainGestureActive)
            project.beginUndoTransaction ("Change track gain");
        auto settings = project.getTrackMixerSettings();
        settings.gainDecibels = trackGainSlider.getValue();
        const auto result = project.setTrackMixerSettings (settings);
        if (result.failed())
            projectStatusMessage = result.getErrorMessage();
    };
    addAndMakeVisible (trackGainSlider);

    trackPanSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    trackPanSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 24);
    trackPanSlider.setRange (-1.0, 1.0, 0.05);
    trackPanSlider.onDragStart = [this]
    {
        trackPanGestureActive = true;
        project.beginUndoTransaction ("Change track pan");
    };
    trackPanSlider.onDragEnd = [this] { trackPanGestureActive = false; };
    trackPanSlider.onValueChange = [this]
    {
        if (refreshingProjectControls)
            return;

        if (! trackPanGestureActive)
            project.beginUndoTransaction ("Change track pan");
        auto settings = project.getTrackMixerSettings();
        settings.pan = trackPanSlider.getValue();
        const auto result = project.setTrackMixerSettings (settings);
        if (result.failed())
            projectStatusMessage = result.getErrorMessage();
    };
    addAndMakeVisible (trackPanSlider);

    trackMuteButton.onClick = [this]
    {
        if (refreshingProjectControls)
            return;
        project.beginUndoTransaction ("Toggle track mute");
        auto settings = project.getTrackMixerSettings();
        settings.muted = trackMuteButton.getToggleState();
        const auto result = project.setTrackMixerSettings (settings);
        if (result.failed())
            projectStatusMessage = result.getErrorMessage();
    };
    trackSoloButton.onClick = [this]
    {
        if (refreshingProjectControls)
            return;
        project.beginUndoTransaction ("Toggle track solo");
        auto settings = project.getTrackMixerSettings();
        settings.solo = trackSoloButton.getToggleState();
        const auto result = project.setTrackMixerSettings (settings);
        if (result.failed())
            projectStatusMessage = result.getErrorMessage();
    };

    snapCombo.addItem ("1/32", 1);
    snapCombo.addItem ("1/16", 2);
    snapCombo.addItem ("1/8", 3);
    snapCombo.addItem ("1/4", 4);
    snapCombo.onChange = [this]
    {
        if (! refreshingProjectControls)
        {
            project.beginUndoTransaction ("Change grid snap");
            project.setSnapBeats (snapForComboId (snapCombo.getSelectedId()));
        }
    };
    addAndMakeVisible (snapCombo);

    loopLengthCombo.addItem ("1 bar", 1);
    loopLengthCombo.addItem ("2 bars", 2);
    loopLengthCombo.addItem ("4 bars", 3);
    loopLengthCombo.addItem ("8 bars", 4);
    loopLengthCombo.onChange = [this]
    {
        if (! refreshingProjectControls)
        {
            project.beginUndoTransaction ("Change loop length");
            project.setLoopLengthBeats (loopForComboId (loopLengthCombo.getSelectedId()));
        }
    };
    addAndMakeVisible (loopLengthCombo);

    velocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 24);
    velocitySlider.setRange (1.0, 127.0, 1.0);
    velocitySlider.setValue (96.0, juce::dontSendNotification);
    velocitySlider.setEnabled (false);
    velocitySlider.onDragStart = [this]
    {
        velocityGestureActive = true;
        project.beginUndoTransaction (velocityTransactionName());
    };
    velocitySlider.onDragEnd = [this] { velocityGestureActive = false; };
    velocitySlider.onValueChange = [this]
    {
        if (refreshingProjectControls || pianoRoll == nullptr)
            return;

        const auto selectedIds = pianoRoll->getSelectedNotes();
        if (selectedIds.empty())
            return;

        if (! velocityGestureActive)
            project.beginUndoTransaction (velocityTransactionName());

        // The slider sets one absolute velocity across the whole selection, so a
        // multi-note gesture stays one Undo step.
        const auto velocity = juce::roundToInt (velocitySlider.getValue());
        for (const auto& id : selectedIds)
        {
            auto note = project.findNote (id);
            if (! note.has_value())
                continue;
            note->velocity = velocity;
            project.updateNote (*note);
        }
    };
    addAndMakeVisible (velocitySlider);

    pianoRoll = std::make_unique<PianoRoll> (project);
    pianoRoll->setSelectionChangedCallback ([this] (const juce::String& id) { selectedNoteChanged (id); });
    pianoRoll->setStatusMessageCallback ([this] (const juce::String& message)
                                         {
                                             projectStatusMessage = message;
                                             updateStatus();
                                         });
    addAndMakeVisible (*pianoRoll);

    keyboard = std::make_unique<juce::MidiKeyboardComponent> (keyboardState,
                                                              juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard->setAvailableRange (36, 96);
    keyboard->setLowestVisibleKey (43);
    keyboard->setKeyWidth (22.0f);
    keyboard->setColour (juce::MidiKeyboardComponent::whiteNoteColourId,
                         juce::Colour::fromRGB (224, 232, 240));
    keyboard->setColour (juce::MidiKeyboardComponent::blackNoteColourId,
                         juce::Colour::fromRGB (19, 27, 39));
    keyboard->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, primary);
    addAndMakeVisible (*keyboard);

    project.setChangeCallback ([this] { projectChanged(); });
    refreshProjectControls();
}

void MainEditorComponent::initialiseAudioAndPlugin()
{
    pluginFormats.addFormat (std::make_unique<juce::VST3PluginFormat>());

    std::unique_ptr<juce::XmlElement> savedDeviceState;
    if (settingsFile != nullptr)
    {
        const auto savedXml = settingsFile->getValue ("audioDeviceState");
        if (savedXml.isNotEmpty())
            savedDeviceState = juce::parseXML (savedXml);
    }

    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = 48000.0;
    preferred.bufferSize = 512;
    deviceStartupError = deviceManager.initialise (0, 2, savedDeviceState.get(), true, {}, &preferred);
    deviceManager.addChangeListener (this);

    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager,
                                                                           0, 0,
                                                                           2, 2,
                                                                           true, false,
                                                                           true, false);
    deviceSelector->setItemHeight (24);
    addAndMakeVisible (*deviceSelector);

    const auto inventoryResult = loadFirstAcceptedInstrument (inventoryPath, quarantinePath, pluginRecord);
    if (inventoryResult.failed())
    {
        startupError = inventoryResult.getErrorMessage();
        updateStatus();
        return;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    const auto rate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    for (int trackIndex = 0; trackIndex < SongProject::maxProjectTracks; ++trackIndex)
    {
        juce::String loadError;
        auto plugin = pluginFormats.createPluginInstance (pluginRecord.description,
                                                          rate,
                                                          blockSize,
                                                          loadError);
        if (plugin == nullptr)
        {
            startupError = "Accepted plug-in instance " + juce::String (trackIndex + 1)
                           + " could not be loaded: " + loadError;
            updateStatus();
            return;
        }

        if (plugin->getParameters().size() != pluginRecord.expectedParameterCount)
        {
            startupError = "Loaded plug-in parameter count differs from the accepted scan; rescan is required";
            updateStatus();
            return;
        }

        const auto install = engine.setPluginForTrack (static_cast<std::size_t> (trackIndex),
                                                       std::move (plugin));
        if (install.failed())
        {
            startupError = install.getErrorMessage();
            updateStatus();
            return;
        }

        const auto capture = engine.capturePluginStateForTrack (
            static_cast<std::size_t> (trackIndex), initialPluginStates[trackIndex]);
        if (capture.failed() || initialPluginStates[trackIndex].getSize() == 0)
        {
            startupError = "Surge state capture failed for runtime slot "
                           + juce::String (trackIndex + 1) + ": "
                           + capture.getErrorMessage();
            updateStatus();
            return;
        }

        slotAcceptedLiveSoundSha256[trackIndex] =
            juce::SHA256 (initialPluginStates[trackIndex]).toHexString();
    }

    {
        const juce::ScopedValueSetter<bool> suppress (suppressProjectChanges, true);
        project.setPluginMetadata (pluginRecord.identifier,
                                   pluginRecord.description.name,
                                   pluginRecord.description.manufacturerName,
                                   pluginRecord.description.version);
        project.setPluginState (initialPluginStates[0]);
    }
    slotProjectStateSha256[0] = project.getPluginStateSha256 (0);
    activeSoundTrackId = project.getTrackId();
    acceptedLiveSoundSha256 = slotAcceptedLiveSoundSha256[0];
    auditionedSoundSha256 = acceptedLiveSoundSha256;

    if (rate == 44100.0 || rate == 48000.0 || rate == 88200.0 || rate == 96000.0)
        project.setSampleRate (juce::roundToInt (rate));
    project.markClean();

    keyboardState.addListener (&engine.getMidiCollector());
    keyboardListenerRegistered = true;
    deviceManager.addMidiInputDeviceCallback ({}, &engine.getMidiCollector());
    midiCallbackRegistered = true;
    deviceManager.addAudioCallback (&engine);
    audioCallbackRegistered = true;

    for (int trackIndex = 0; trackIndex < SongProject::maxProjectTracks; ++trackIndex)
    {
        juce::MemoryBlock preparedLiveState;
        const auto restore = engine.restorePluginStateForTrack (
            static_cast<std::size_t> (trackIndex),
            initialPluginStates[trackIndex],
            &preparedLiveState);
        if (restore.failed())
        {
            startupError = "Prepared Surge state restore failed for runtime slot "
                           + juce::String (trackIndex + 1) + ": "
                           + restore.getErrorMessage();
            break;
        }

        slotAcceptedLiveSoundSha256[trackIndex] =
            juce::SHA256 (preparedLiveState).toHexString();
    }

    acceptedLiveSoundSha256 = slotAcceptedLiveSoundSha256[0];
    auditionedSoundSha256 = acceptedLiveSoundSha256;
    refreshProjectControls();
    updateStatus();
}

juce::AudioPluginInstance* MainEditorComponent::getActivePlugin() const noexcept
{
    const auto activeTrack = project.getActiveTrackIndex();
    return activeTrack >= 0
               ? engine.getPluginForTrack (static_cast<std::size_t> (activeTrack))
               : nullptr;
}

bool MainEditorComponent::canChangeTrackContext()
{
    if (soundCandidate.has_value())
    {
        projectStatusMessage = "APPLY OR REJECT SOUND B BEFORE CHANGING TRACKS";
        updateStatus();
        return false;
    }

    if (hasPendingEditPreview())
    {
        projectStatusMessage = "APPLY OR REJECT NOTE B BEFORE CHANGING TRACKS";
        updateStatus();
        return false;
    }

    if (hasUncapturedLiveSoundState())
    {
        projectStatusMessage = "CAPTURE B OR RESTORE A BEFORE CHANGING TRACKS";
        updateStatus();
        return false;
    }

    return true;
}

void MainEditorComponent::selectTrack (int trackIndex)
{
    const auto currentTrack = project.getActiveTrackIndex();
    if (trackIndex == currentTrack)
        return;

    if (trackIndex < 0 || trackIndex >= project.getTrackCount()
        || ! canChangeTrackContext())
    {
        const juce::ScopedValueSetter<bool> refreshing (refreshingProjectControls, true);
        trackSelector.setSelectedId (currentTrack + 1, juce::dontSendNotification);
        return;
    }

    engine.panic();
    pluginEditorWindow.reset();
    pluginEditorTrackId.clear();
    pluginEditorTrackIndex = -1;
    if (pianoRoll != nullptr)
        pianoRoll->setSelectedNote ({});

    if (project.setActiveTrackIndex (trackIndex))
        projectStatusMessage = "SELECTED TRACK " + juce::String (trackIndex + 1)
                               + "  /  " + project.getTrackName();
}

void MainEditorComponent::addInstrumentTrack()
{
    if (! canChangeTrackContext())
        return;

    if (project.getTrackCount() >= SongProject::maxProjectTracks)
    {
        projectStatusMessage = "THIS M6 SLICE SUPPORTS TWO INSTRUMENT TRACKS";
        updateStatus();
        return;
    }

    pluginEditorWindow.reset();
    pluginEditorTrackId.clear();
    pluginEditorTrackIndex = -1;
    slotProjectStateSha256[1].clear();
    juce::String newTrackId;
    const auto result = project.duplicateActiveTrack (&newTrackId);
    if (result.failed())
    {
        showError ("Could not add instrument track", result.getErrorMessage());
        return;
    }

    if (pianoRoll != nullptr)
        pianoRoll->setSelectedNote ({});
    projectStatusMessage = "TRACK 2 ADDED  /  INDEPENDENT SURGE INSTANCE READY";
    refreshProjectControls();
}

void MainEditorComponent::removeActiveTrack()
{
    if (! canChangeTrackContext())
        return;

    if (project.getTrackCount() <= 1)
    {
        projectStatusMessage = "A SONG MUST KEEP AT LEAST ONE INSTRUMENT TRACK";
        updateStatus();
        return;
    }

    const auto removedName = project.getTrackName();
    pluginEditorWindow.reset();
    pluginEditorTrackId.clear();
    pluginEditorTrackIndex = -1;
    for (auto& hash : slotProjectStateSha256)
        hash.clear();

    const auto result = project.removeTrack (project.getTrackId());
    if (result.failed())
    {
        showError ("Could not remove instrument track", result.getErrorMessage());
        return;
    }

    if (pianoRoll != nullptr)
        pianoRoll->setSelectedNote ({});
    projectStatusMessage = "REMOVED " + removedName + "  /  UNDO RESTORES IT";
    refreshProjectControls();
}

void MainEditorComponent::moveActiveTrack (int offset)
{
    if (! canChangeTrackContext())
        return;

    const auto current = project.getActiveTrackIndex();
    const auto target = current + offset;
    if (target < 0 || target >= project.getTrackCount())
        return;

    pluginEditorWindow.reset();
    pluginEditorTrackId.clear();
    pluginEditorTrackIndex = -1;
    for (auto& hash : slotProjectStateSha256)
        hash.clear();

    const auto result = project.moveTrack (project.getTrackId(), target);
    if (result.failed())
    {
        showError ("Could not reorder instrument track", result.getErrorMessage());
        return;
    }

    projectStatusMessage = "TRACK ORDER CHANGED  /  UNDO RESTORES IT";
    refreshProjectControls();
}

juce::Result MainEditorComponent::restoreRuntimeSlotsFromProject (
    const SongProject& source,
    std::array<juce::String, SongProject::maxProjectTracks>& liveStateHashes)
{
    std::array<juce::MemoryBlock, SongProject::maxProjectTracks> previousStates;
    std::array<bool, SongProject::maxProjectTracks> previousStateCaptured {};

    for (int trackIndex = 0; trackIndex < SongProject::maxProjectTracks; ++trackIndex)
    {
        if (engine.getPluginForTrack (static_cast<std::size_t> (trackIndex)) == nullptr)
            continue;

        previousStateCaptured[trackIndex] = engine.capturePluginStateForTrack (
                                                static_cast<std::size_t> (trackIndex),
                                                previousStates[trackIndex]).wasOk();
    }

    for (int trackIndex = 0; trackIndex < source.getTrackCount(); ++trackIndex)
    {
        if (engine.getPluginForTrack (static_cast<std::size_t> (trackIndex)) == nullptr)
            return juce::Result::fail ("Runtime slot " + juce::String (trackIndex + 1)
                                       + " has no accepted Surge XT instance");

        juce::MemoryBlock state;
        const auto stateResult = source.getPluginStateForTrack (trackIndex, state);
        if (stateResult.failed())
            return stateResult;

        juce::MemoryBlock liveState;
        const auto restore = engine.restorePluginStateForTrack (
            static_cast<std::size_t> (trackIndex), state, &liveState);
        if (restore.failed())
        {
            for (int rollback = 0; rollback < SongProject::maxProjectTracks; ++rollback)
                if (previousStateCaptured[rollback])
                    engine.restorePluginStateForTrack (static_cast<std::size_t> (rollback),
                                                       previousStates[rollback]);

            return juce::Result::fail ("Track " + juce::String (trackIndex + 1)
                                       + " Surge state restore failed: "
                                       + restore.getErrorMessage());
        }

        liveStateHashes[trackIndex] = juce::SHA256 (liveState).toHexString();
    }

    return juce::Result::ok();
}

juce::Result MainEditorComponent::synchronisePluginSlotsFromProject (bool forceRestore)
{
    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
    {
        const auto desiredHash = project.getPluginStateSha256 (trackIndex);
        if (! forceRestore
            && desiredHash.isNotEmpty()
            && slotProjectStateSha256[trackIndex].equalsIgnoreCase (desiredHash))
            continue;

        juce::MemoryBlock state;
        const auto stateResult = project.getPluginStateForTrack (trackIndex, state);
        if (stateResult.failed())
            return stateResult;

        juce::MemoryBlock liveState;
        const auto restore = engine.restorePluginStateForTrack (
            static_cast<std::size_t> (trackIndex), state, &liveState);
        if (restore.failed())
            return juce::Result::fail ("Track " + juce::String (trackIndex + 1)
                                       + " Surge state restore failed: "
                                       + restore.getErrorMessage());

        slotProjectStateSha256[trackIndex] = desiredHash;
        slotAcceptedLiveSoundSha256[trackIndex] = juce::SHA256 (liveState).toHexString();
        if (trackIndex == project.getActiveTrackIndex())
        {
            acceptedLiveSoundSha256 = slotAcceptedLiveSoundSha256[trackIndex];
            auditionedSoundSha256 = acceptedLiveSoundSha256;
        }
    }

    for (int trackIndex = project.getTrackCount();
         trackIndex < SongProject::maxProjectTracks;
         ++trackIndex)
        slotProjectStateSha256[trackIndex].clear();

    updateActiveSoundTracking();
    return juce::Result::ok();
}

void MainEditorComponent::publishProjectMixerSnapshot (
    const SequenceSnapshot* activeTrackOverride)
{
    MixerSnapshot mixer;
    mixer.trackCount = static_cast<std::size_t> (
        juce::jlimit (0, SongProject::maxProjectTracks, project.getTrackCount()));

    for (int trackIndex = 0; trackIndex < static_cast<int> (mixer.trackCount); ++trackIndex)
    {
        const auto mixerSettings = project.getTrackMixerSettings (trackIndex);
        const auto midiRouting = project.getTrackMidiRouting (trackIndex);
        auto& runtimeTrack = mixer.tracks[static_cast<std::size_t> (trackIndex)];
        runtimeTrack.sequence = activeTrackOverride != nullptr
                                        && trackIndex == project.getActiveTrackIndex()
                                    ? *activeTrackOverride
                                    : project.createSequenceSnapshotForTrack (trackIndex);
        runtimeTrack.gainLinear = juce::Decibels::decibelsToGain (
            static_cast<float> (mixerSettings.gainDecibels));
        runtimeTrack.pan = static_cast<float> (mixerSettings.pan);
        runtimeTrack.midiInputChannel = midiRouting.inputChannel;
        runtimeTrack.midiOutputChannel = midiRouting.outputChannel;
        runtimeTrack.enabled = engine.getPluginForTrack (
                                   static_cast<std::size_t> (trackIndex)) != nullptr;
        runtimeTrack.muted = mixerSettings.muted;
        runtimeTrack.solo = mixerSettings.solo;
    }

    engine.setMixerSnapshot (mixer);
}

void MainEditorComponent::updateActiveSoundTracking()
{
    const auto activeTrack = project.getActiveTrackIndex();
    if (activeTrack < 0 || activeTrack >= SongProject::maxProjectTracks)
        return;

    const auto selectedTrackId = project.getTrackId (activeTrack);
    const auto trackChanged = selectedTrackId != activeSoundTrackId;
    if (trackChanged && soundCandidate.has_value())
        clearSoundCandidate();

    activeSoundTrackId = selectedTrackId;
    acceptedLiveSoundSha256 = slotAcceptedLiveSoundSha256[activeTrack].isNotEmpty()
                                  ? slotAcceptedLiveSoundSha256[activeTrack]
                                  : project.getPluginStateSha256 (activeTrack);
    if (trackChanged)
    {
        auditionedSoundSha256 = acceptedLiveSoundSha256;
        candidateLiveSoundSha256.clear();
    }
}

void MainEditorComponent::openPluginEditor()
{
    auto* plugin = getActivePlugin();
    if (plugin == nullptr || ! pluginRecord.hasEditor)
        return;

    if (pluginEditorWindow == nullptr)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
        if (editor == nullptr)
            return;

        pluginEditorWindow = std::make_unique<PluginEditorWindow> (std::move (editor),
                                                                   engine,
                                                                   keyboardState);
        pluginEditorTrackId = project.getTrackId();
        pluginEditorTrackIndex = project.getActiveTrackIndex();
    }

    pluginEditorWindow->setVisible (true);
    pluginEditorWindow->toFront (true);
    auditionedSoundSha256.clear();
    projectStatusMessage = "LIVE SURGE EDIT  /  CAPTURE B TO COMPARE OR KEEP A UNCHANGED";
    refreshSoundControls();
}

void MainEditorComponent::captureSoundCandidate()
{
    if (hasPendingEditPreview())
    {
        projectStatusMessage = "APPLY OR REJECT THE NOTE PROPOSAL BEFORE CAPTURING SOUND B";
        updateStatus();
        return;
    }

    const auto activeTrack = project.getActiveTrackIndex();
    juce::MemoryBlock state;
    const auto result = engine.capturePluginStateForTrack (
        static_cast<std::size_t> (activeTrack), state);
    if (result.failed())
    {
        showError ("Could not capture sound B", result.getErrorMessage());
        return;
    }

    auto name = soundNameEditor.getText().trim().substring (0, 80);
    if (name.isEmpty())
        name = "Captured sound";

    const auto hash = juce::SHA256 (state).toHexString();
    soundCandidate = PluginSoundSnapshot { name, std::move (state), hash };
    soundCandidateTrackId = project.getTrackId();
    slotProjectStateSha256[activeTrack].clear();
    candidateLiveSoundSha256 = soundCandidate->stateSha256;
    auditionedSoundSha256 = candidateLiveSoundSha256;
    const auto acceptedHash = acceptedLiveSoundSha256.isNotEmpty()
                                  ? acceptedLiveSoundSha256
                                  : project.getPluginStateSha256();
    projectStatusMessage = candidateLiveSoundSha256.equalsIgnoreCase (acceptedHash)
                               ? "B CAPTURED  /  STATE MATCHES A"
                               : "B CAPTURED  /  AUDITION A-B BEFORE APPLY";
    refreshSoundControls();
}

void MainEditorComponent::auditionProjectSound()
{
    if (hasPendingEditPreview())
        return;

    restoreProjectSound ("AUDITIONING A  /  PROJECT REMAINS UNCHANGED");
}

void MainEditorComponent::auditionSoundCandidate()
{
    if (hasPendingEditPreview())
        return;

    if (soundCandidate.has_value())
        restoreSoundSnapshot (*soundCandidate,
                              "AUDITIONING B  /  PROJECT REMAINS UNCHANGED",
                              &candidateLiveSoundSha256);
}

void MainEditorComponent::applySoundCandidate()
{
    if (! soundCandidate.has_value() || hasPendingEditPreview()
        || soundCandidateTrackId != project.getTrackId())
        return;

    auto candidate = *soundCandidate;
    const auto editedName = soundNameEditor.getText().trim().substring (0, 80);
    if (editedName.isNotEmpty())
        candidate.name = editedName;

    PluginSoundSnapshot acceptedBefore;
    const auto acceptedResult = project.getPluginSoundSnapshot (acceptedBefore);
    if (acceptedResult.failed())
    {
        showError ("Could not read project sound", acceptedResult.getErrorMessage());
        return;
    }

    if (! restoreSoundSnapshot (candidate, "APPLYING B", &candidateLiveSoundSha256))
        return;

    const auto applyResult = project.applyPluginSound (candidate.name, candidate.state);
    if (applyResult.failed())
    {
        restoreSoundSnapshot (acceptedBefore,
                              "RESTORED A AFTER APPLY FAILURE",
                              &acceptedLiveSoundSha256);
        showError ("Could not apply sound B", applyResult.getErrorMessage());
        return;
    }

    acceptedLiveSoundSha256 = candidateLiveSoundSha256.isNotEmpty()
                                  ? candidateLiveSoundSha256
                                  : project.getPluginStateSha256();
    const auto activeTrack = project.getActiveTrackIndex();
    slotProjectStateSha256[activeTrack] = project.getPluginStateSha256();
    slotAcceptedLiveSoundSha256[activeTrack] = acceptedLiveSoundSha256;
    auditionedSoundSha256 = acceptedLiveSoundSha256;
    clearSoundCandidate();
    projectStatusMessage = "SOUND APPLIED  /  UNDO RESTORES THE PREVIOUS SOUND";
    refreshProjectControls();
}

void MainEditorComponent::rejectSoundCandidate()
{
    if (! soundCandidate.has_value() || hasPendingEditPreview()
        || soundCandidateTrackId != project.getTrackId())
        return;

    if (! restoreProjectSound ("B REJECTED  /  A RESTORED"))
        return;

    clearSoundCandidate();
    projectStatusMessage = "B REJECTED  /  PROJECT SOUND UNCHANGED";
    refreshSoundControls();
}

void MainEditorComponent::performUndoRedo (bool redo)
{
    const auto previousTrackId = project.getTrackId();
    const auto previousHash = project.getPluginStateSha256();
    const auto changed = redo ? project.redo() : project.undo();
    if (! changed)
        return;

    if (previousTrackId != project.getTrackId()
        || ! previousHash.equalsIgnoreCase (project.getPluginStateSha256()))
    {
        clearSoundCandidate();
        projectStatusMessage = redo
                                   ? "REDO RESTORED THE PROJECT TRACK STATE"
                                   : "UNDO RESTORED THE PREVIOUS TRACK STATE";
    }

    refreshProjectControls();
}

bool MainEditorComponent::restoreSoundSnapshot (const PluginSoundSnapshot& snapshot,
                                                const juce::String& actionLabel,
                                                juce::String* liveStateSha256)
{
    if (snapshot.state.getSize() == 0
        || ! snapshot.stateSha256.equalsIgnoreCase (juce::SHA256 (snapshot.state).toHexString()))
    {
        showError ("Invalid sound snapshot", "The sound snapshot failed its state integrity check.");
        return false;
    }

    const auto activeTrack = project.getActiveTrackIndex();
    if (activeTrack < 0 || getActivePlugin() == nullptr)
    {
        showError ("Could not restore Surge XT", "The active track has no runtime instrument.");
        return false;
    }

    juce::MemoryBlock liveState;
    const auto restore = engine.restorePluginStateForTrack (
        static_cast<std::size_t> (activeTrack), snapshot.state, &liveState);
    if (restore.failed())
    {
        showError ("Could not restore Surge XT", restore.getErrorMessage());
        return false;
    }

    const auto restoredLiveHash = juce::SHA256 (liveState).toHexString();
    if (liveStateSha256 != nullptr)
        *liveStateSha256 = restoredLiveHash;
    auditionedSoundSha256 = restoredLiveHash;
    slotProjectStateSha256[activeTrack].clear();
    projectStatusMessage = actionLabel;
    refreshSoundControls();
    return true;
}

bool MainEditorComponent::restoreProjectSound (const juce::String& actionLabel)
{
    PluginSoundSnapshot accepted;
    const auto result = project.getPluginSoundSnapshot (accepted);
    if (result.failed())
    {
        showError ("Could not read project sound", result.getErrorMessage());
        return false;
    }

    if (! restoreSoundSnapshot (accepted, actionLabel, &acceptedLiveSoundSha256))
        return false;

    const auto activeTrack = project.getActiveTrackIndex();
    slotProjectStateSha256[activeTrack] = project.getPluginStateSha256();
    slotAcceptedLiveSoundSha256[activeTrack] = acceptedLiveSoundSha256;
    return true;
}

bool MainEditorComponent::hasUncapturedLiveSoundState()
{
    const auto activeTrack = project.getActiveTrackIndex();
    if (activeTrack < 0 || getActivePlugin() == nullptr)
        return false;

    juce::MemoryBlock liveState;
    if (engine.capturePluginStateForTrack (static_cast<std::size_t> (activeTrack),
                                           liveState).failed()
        || liveState.getSize() == 0)
        return true;

    const auto liveHash = juce::SHA256 (liveState).toHexString();
    const auto acceptedHash = acceptedLiveSoundSha256.isNotEmpty()
                                  ? acceptedLiveSoundSha256
                                  : project.getPluginStateSha256();
    if (liveHash.equalsIgnoreCase (acceptedHash))
        return false;
    if (soundCandidate.has_value()
        && (liveHash.equalsIgnoreCase (candidateLiveSoundSha256)
            || liveHash.equalsIgnoreCase (soundCandidate->stateSha256)))
        return false;
    return true;
}

void MainEditorComponent::clearSoundCandidate()
{
    soundCandidate.reset();
    soundCandidateTrackId.clear();
    candidateLiveSoundSha256.clear();
    soundNameEditor.setText ("Captured sound", false);
}

void MainEditorComponent::refreshSoundControls()
{
    const auto ready = getActivePlugin() != nullptr;
    const auto editLaneClear = ! hasPendingEditPreview();
    const auto savedAcceptedHash = project.getPluginStateSha256();
    const auto acceptedHash = acceptedLiveSoundSha256.isNotEmpty()
                                  ? acceptedLiveSoundSha256
                                  : savedAcceptedHash;
    const auto candidateHash = soundCandidate.has_value()
                                   ? (candidateLiveSoundSha256.isNotEmpty()
                                          ? candidateLiveSoundSha256
                                          : soundCandidate->stateSha256)
                                   : juce::String {};
    auto description = "A  /  " + project.getPluginSoundName();
    if (acceptedHash.isNotEmpty())
        description += "  /  " + shortStateHash (acceptedHash);

    if (soundCandidate.has_value())
    {
        description += "       B  /  " + soundCandidate->name + "  /  "
                       + shortStateHash (candidateHash);
    }
    else
        description += "       B  /  CAPTURE FROM LIVE SURGE";

    soundWorkflowLabel.setText (description, juce::dontSendNotification);
    auto tooltip = "Saved A: " + savedAcceptedHash;
    if (! acceptedHash.equalsIgnoreCase (savedAcceptedHash))
        tooltip += "\nLive-equivalent A: " + acceptedHash;
    if (soundCandidate.has_value())
        tooltip += "\nB snapshot: " + soundCandidate->stateSha256
                   + "\nLive-equivalent B: "
                   + (candidateLiveSoundSha256.isNotEmpty()
                          ? candidateLiveSoundSha256
                          : soundCandidate->stateSha256);
    soundWorkflowLabel.setTooltip (tooltip);

    const auto candidateDiffers = soundCandidate.has_value()
                                  && (! candidateHash.equalsIgnoreCase (acceptedHash)
                                      || soundCandidate->name != project.getPluginSoundName());
    auditionProjectSoundButton.setEnabled (ready && editLaneClear && acceptedHash.isNotEmpty());
    captureSoundButton.setEnabled (ready && editLaneClear);
    auditionCandidateButton.setEnabled (ready && editLaneClear && soundCandidate.has_value());
    applySoundButton.setEnabled (ready && editLaneClear && candidateDiffers);
    rejectSoundButton.setEnabled (ready && editLaneClear && soundCandidate.has_value());
    soundNameEditor.setEnabled (ready && editLaneClear);

    auditionProjectSoundButton.setToggleState (auditionedSoundSha256.equalsIgnoreCase (acceptedHash),
                                                juce::dontSendNotification);
    auditionCandidateButton.setToggleState (soundCandidate.has_value()
                                                && auditionedSoundSha256.equalsIgnoreCase (candidateHash),
                                             juce::dontSendNotification);
}

bool MainEditorComponent::hasPendingEditPreview() const noexcept
{
    return editPreview.has_value() && editPreview->isPending();
}

void MainEditorComponent::refreshShelfControls()
{
    const juce::ScopedValueSetter<bool> refreshing (refreshingProjectControls, true);
    const auto previous = shelfCombo.getText();
    shelfCombo.clear (juce::dontSendNotification);

    int itemId = 1;
    for (const auto& entry : soundShelf.getEntries())
        shelfCombo.addItem (entry.name, itemId++);

    if (previous.isNotEmpty())
        for (int index = 0; index < shelfCombo.getNumItems(); ++index)
            if (shelfCombo.getItemText (index) == previous)
                shelfCombo.setSelectedItemIndex (index, juce::dontSendNotification);

    if (shelfCombo.getSelectedId() == 0 && shelfCombo.getNumItems() > 0)
        shelfCombo.setSelectedItemIndex (0, juce::dontSendNotification);

    shelfLabel.setText (soundShelf.getEntryCount() > 0
                            ? "SHELF " + juce::String (soundShelf.getEntryCount())
                            : "SHELF",
                        juce::dontSendNotification);

    const auto ready = getActivePlugin() != nullptr;
    const auto laneClear = ! hasPendingEditPreview();
    const auto hasSelection = shelfCombo.getSelectedId() != 0;
    shelfCombo.setEnabled (ready && soundShelf.getEntryCount() > 0);
    loadShelfButton.setEnabled (ready && laneClear && hasSelection);
    removeShelfButton.setEnabled (ready && hasSelection);
    saveShelfButton.setEnabled (ready && laneClear
                                && soundShelf.getEntryCount()
                                       < static_cast<int> (SoundShelf::maximumEntries));
}

void MainEditorComponent::saveSoundToShelf()
{
    if (getActivePlugin() == nullptr)
        return;

    SoundShelfEntry entry;
    // A pending candidate is what the user is auditioning, so it is the sound they
    // mean to keep. Otherwise the accepted project sound is shelved.
    if (soundCandidate.has_value())
    {
        entry.name = soundCandidate->name;
        entry.state = soundCandidate->state;
        entry.stateSha256 = soundCandidate->stateSha256;
    }
    else
    {
        PluginSoundSnapshot accepted;
        const auto acceptedResult = project.getPluginSoundSnapshot (accepted);
        if (acceptedResult.failed())
        {
            showError ("Could not read the project sound", acceptedResult.getErrorMessage());
            return;
        }
        entry.name = accepted.name;
        entry.state = accepted.state;
        entry.stateSha256 = accepted.stateSha256;
    }

    const auto typedName = soundNameEditor.getText().trim().substring (0, SoundShelf::maximumNameLength);
    if (typedName.isNotEmpty())
        entry.name = typedName;

    entry.pluginIdentifier = pluginRecord.identifier;
    entry.pluginName = pluginRecord.description.name;
    entry.vendor = pluginRecord.description.manufacturerName;
    entry.version = pluginRecord.description.version;

    const auto source = soundCandidate.has_value() ? juce::String ("B") : juce::String ("A");
    const auto added = soundShelf.add (entry);
    if (added.failed())
    {
        projectStatusMessage = "SHELF SAVE REFUSED  /  " + added.getErrorMessage().toUpperCase();
        updateStatus();
        showError ("Could not save to the shelf", added.getErrorMessage());
        return;
    }

    const auto written = soundShelf.saveTo (soundShelfPath);
    if (written.failed())
    {
        soundShelf.remove (entry.name);
        projectStatusMessage = "SHELF WRITE FAILED";
        updateStatus();
        showError ("Could not write the shelf", written.getErrorMessage());
        return;
    }

    shelfCombo.setText (entry.name, juce::dontSendNotification);
    projectStatusMessage = "SHELVED SOUND " + source + "  /  " + entry.name.toUpperCase();
    refreshProjectControls();
}

juce::Result MainEditorComponent::loadSoundFromShelf (bool reportFailure)
{
    if (getActivePlugin() == nullptr || hasPendingEditPreview())
        return juce::Result::fail ("The sound lane is not available");

    const auto* entry = soundShelf.find (shelfCombo.getText());
    if (entry == nullptr)
        return juce::Result::fail ("No shelf sound is chosen");

    // Loading a shelf sound produces candidate B, never a direct change to the
    // accepted sound, so it flows through the accepted Audition/Apply/Reject lane.
    const auto identifierMatches = vst3IdentifiersAreCompatible (entry->pluginIdentifier,
                                                                 pluginRecord.identifier,
                                                                 pluginRecord.description.uniqueId);
    if (! identifierMatches || ! entry->pluginName.equalsIgnoreCase (pluginRecord.description.name))
    {
        const auto detail = "Shelf sound '" + entry->name + "' was captured from "
                            + entry->pluginIdentifier + ", not the accepted "
                            + pluginRecord.identifier + ".";
        projectStatusMessage = "SHELF SOUND REJECTED  /  DIFFERENT INSTRUMENT";
        updateStatus();
        if (reportFailure)
            showError ("Different instrument", detail);
        return juce::Result::fail (detail);
    }

    PluginSoundSnapshot candidate { entry->name, entry->state, entry->stateSha256 };
    if (! restoreSoundSnapshot (candidate, "LOADING SHELF SOUND", &candidateLiveSoundSha256))
        return juce::Result::fail ("The shelf sound could not be restored");

    const auto activeTrack = project.getActiveTrackIndex();
    soundCandidate = std::move (candidate);
    soundCandidateTrackId = project.getTrackId();
    slotProjectStateSha256[activeTrack].clear();
    soundNameEditor.setText (soundCandidate->name, false);
    projectStatusMessage = "SHELF SOUND B READY  /  " + soundCandidate->name.toUpperCase()
                           + "  /  AUDITION A-B BEFORE APPLY";
    refreshProjectControls();
    return juce::Result::ok();
}

void MainEditorComponent::removeSoundFromShelf()
{
    const auto name = shelfCombo.getText();
    const auto removed = soundShelf.remove (name);
    if (removed.failed())
        return;

    const auto written = soundShelf.saveTo (soundShelfPath);
    if (written.failed())
    {
        showError ("Could not write the shelf", written.getErrorMessage());
        return;
    }

    shelfCombo.setText ({}, juce::dontSendNotification);
    projectStatusMessage = "REMOVED SHELF SOUND  /  " + name.toUpperCase();
    refreshProjectControls();
}

void MainEditorComponent::previewSelectedNoteEdit()
{
    if (soundCandidate.has_value())
    {
        projectStatusMessage = "APPLY OR REJECT SOUND B BEFORE PREVIEWING A NOTE EDIT";
        updateStatus();
        return;
    }

    std::vector<SongNote> selected;
    if (pianoRoll != nullptr)
        for (const auto& id : pianoRoll->getSelectedNotes())
            if (const auto note = project.findNote (id))
                selected.push_back (*note);

    if (selected.empty())
    {
        projectStatusMessage = "SELECT A NOTE BEFORE CREATING A PROPOSAL";
        updateStatus();
        return;
    }
    if (selected.size() > maximumEditCommandChanges)
    {
        projectStatusMessage = "SELECT AT MOST " + juce::String (maximumEditCommandChanges)
                               + " NOTES FOR ONE PROPOSAL";
        updateStatus();
        return;
    }
    // Transposing the whole selection must be all or nothing, so a selection that
    // contains the top MIDI pitch is refused rather than silently partly applied.
    if (std::any_of (selected.begin(),
                     selected.end(),
                     [] (const SongNote& note) { return note.midiNote >= 127; }))
    {
        projectStatusMessage = "A SELECTED NOTE IS ALREADY AT THE HIGHEST MIDI PITCH";
        updateStatus();
        return;
    }

    EditCommand command;
    command.projectContentSha256 = project.getContentSha256();
    command.trackId = project.getTrackId();
    command.clipId = project.getClipId();
    command.summary = selected.size() == 1
                          ? "Transpose " + selected.front().id + " up one semitone"
                          : "Transpose " + juce::String (static_cast<int> (selected.size()))
                                + " notes up one semitone";
    for (const auto& note : selected)
    {
        auto after = note;
        ++after.midiNote;
        command.changes.push_back ({ NoteEditAction::update, note.id, after });
    }

    installEditPreview (std::move (command),
                        "NOTE B READY  /  AUDITION A-B BEFORE APPLY");
}

juce::Result MainEditorComponent::readVelocityVariationControls (
    SeededVelocityVariation& variation) const
{
    SeededVelocityVariation requested;

    const auto strengthText = dynamicsStrengthEditor.getText().trim();
    if (strengthText.isEmpty())
        return juce::Result::fail ("Enter a maximum velocity change from 1 through 32");
    const auto maximumDelta = strengthText.getLargeIntValue();
    if (maximumDelta < 1 || maximumDelta > maximumVelocityVariationDelta)
        return juce::Result::fail ("Maximum velocity change must be from 1 through 32");

    const auto seedText = dynamicsSeedEditor.getText().trim();
    if (seedText.isEmpty())
        return juce::Result::fail ("Enter a deterministic seed from 0 through 2147483647");
    const auto seed = seedText.getLargeIntValue();
    if (seed < 0 || seed > maximumVelocityVariationSeed)
        return juce::Result::fail ("Seed must be from 0 through 2147483647");

    requested.maximumDelta = static_cast<int> (maximumDelta);
    requested.seed = seed;

    if (dynamicsScopeCombo.getSelectedId() == velocityScopeWholeLoop)
    {
        const auto notes = project.getNotes();
        requested.noteIds.reserve (notes.size());
        for (const auto& note : notes)
            requested.noteIds.push_back (note.id);
    }
    else if (dynamicsScopeCombo.getSelectedId() == velocityScopeSelectedNote)
    {
        if (pianoRoll != nullptr)
            for (const auto& id : pianoRoll->getSelectedNotes())
                if (project.findNote (id).has_value())
                    requested.noteIds.push_back (id);

        if (requested.noteIds.empty())
            return juce::Result::fail ("Select one or more notes or choose Whole loop");
    }
    else
    {
        return juce::Result::fail ("Choose a dynamics target");
    }

    if (requested.noteIds.empty() || requested.noteIds.size() > 128)
        return juce::Result::fail ("Dynamics must target from 1 through 128 notes");

    variation = std::move (requested);
    return juce::Result::ok();
}

void MainEditorComponent::previewVelocityVariation()
{
    if (soundCandidate.has_value())
    {
        projectStatusMessage = "APPLY OR REJECT SOUND B BEFORE PREVIEWING A NOTE EDIT";
        updateStatus();
        return;
    }

    SeededVelocityVariation variation;
    const auto settingsResult = readVelocityVariationControls (variation);
    if (settingsResult.failed())
    {
        projectStatusMessage = "DYNAMICS SETTINGS ERROR  /  " + settingsResult.getErrorMessage();
        updateStatus();
        showError ("Could not preview dynamics", settingsResult.getErrorMessage());
        return;
    }

    EditCommand command;
    const auto result = resolveSeededVelocityVariation (project, variation, command);
    if (result.failed())
    {
        projectStatusMessage = "DYNAMICS PROPOSAL ERROR  /  " + result.getErrorMessage();
        updateStatus();
        showError ("Could not preview loop dynamics", result.getErrorMessage());
        return;
    }

    const auto targetCount = static_cast<int> (variation.noteIds.size());
    installEditPreview (std::move (command),
                        "DYNAMICS B READY  /  " + juce::String (targetCount)
                            + (targetCount == 1 ? " NOTE  /  MAX +/-" : " NOTES  /  MAX +/-")
                            + juce::String (variation.maximumDelta)
                            + "  /  SEED "
                            + juce::String::formatted ("%lld",
                                                       static_cast<long long> (variation.seed)));
}

juce::var MainEditorComponent::runSelectionSelfTest()
{
    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);
    resultObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);

    clearEditPreview (true);
    clearSoundCandidate();
    project.markClean();

    const auto notes = project.getNotes();
    if (getActivePlugin() == nullptr || pianoRoll == nullptr || notes.size() < 3)
    {
        resultObject->setProperty ("passed", false);
        resultObject->setProperty ("error", "The starter project needs an instrument and three notes");
        return result;
    }

    const std::vector<juce::String> chosen { notes[0].id, notes[1].id, notes[2].id };
    pianoRoll->setSelectedNotes (chosen);
    const auto selectedThree = pianoRoll->getSelectedNotes().size() == 3
                               && pianoRoll->getSelectedNote() == chosen.back();

    // Ids that name no live note must be dropped rather than carried as dead weight.
    auto withGhost = chosen;
    withGhost.push_back ("note-does-not-exist");
    withGhost.push_back (chosen.front());
    pianoRoll->setSelectedNotes (withGhost);
    const auto rejectedUnknownAndDuplicate = pianoRoll->getSelectedNotes().size() == 3;

    pianoRoll->setSelectedNotes (chosen);
    const auto beforeHash = project.getContentSha256();

    // One slider gesture must write one absolute velocity across the whole selection
    // and collapse into exactly one Undo step.
    velocitySlider.setValue (41.0, juce::sendNotificationSync);
    const auto allVelocitiesSet = std::all_of (chosen.begin(),
                                               chosen.end(),
                                               [this] (const juce::String& id)
                                               {
                                                   const auto note = project.findNote (id);
                                                   return note.has_value() && note->velocity == 41;
                                               });
    performUndoRedo (false);
    const auto velocityUndoneInOneStep = project.getContentSha256() == beforeHash;

    pianoRoll->setSelectedNotes (chosen);
    const auto originalPitches = { notes[0].midiNote, notes[1].midiNote, notes[2].midiNote };
    previewSelectedNoteEdit();
    const auto transposePreviewCreated = hasPendingEditPreview();
    const auto transposeDiffCount = transposePreviewCreated
                                        ? static_cast<int> (editPreview->noteDiffs.size())
                                        : 0;
    const auto activeUnchangedDuringPreview = project.getContentSha256() == beforeHash;
    applyEditPreview();
    auto pitchIterator = originalPitches.begin();
    auto allTransposed = true;
    for (const auto& id : chosen)
    {
        const auto note = project.findNote (id);
        allTransposed = allTransposed && note.has_value()
                        && note->midiNote == *pitchIterator + 1;
        ++pitchIterator;
    }
    performUndoRedo (false);
    const auto transposeUndoneInOneStep = project.getContentSha256() == beforeHash;

    // Removing a selected note must not leave a dangling id behind.
    pianoRoll->setSelectedNotes (chosen);
    project.beginUndoTransaction ("Selection self test removal");
    project.removeNote (chosen.front());
    pianoRoll->pruneSelection();
    const auto prunedAfterRemoval = pianoRoll->getSelectedNotes().size() == 2;
    performUndoRedo (false);

    // Clipboard: copy three notes, paste them, and confirm the copies are new notes
    // that carry the same pitches and relative rhythm.
    pianoRoll->setSelectedNotes (chosen);
    const auto noteCountBeforePaste = static_cast<int> (project.getNotes().size());
    pianoRoll->copySelection();
    const auto copiedToClipboard = pianoRoll->hasClipboardContent();
    const auto pasted = pianoRoll->pasteAtInsertBeat().wasOk();
    const auto pastedIds = pianoRoll->getSelectedNotes();
    const auto pasteAddedNewNotes =
        pasted
        && static_cast<int> (project.getNotes().size()) == noteCountBeforePaste + 3
        && pastedIds.size() == 3
        && std::none_of (pastedIds.begin(),
                         pastedIds.end(),
                         [&chosen] (const juce::String& id)
                         {
                             return std::find (chosen.begin(), chosen.end(), id) != chosen.end();
                         });
    performUndoRedo (false);
    const auto pasteUndoneInOneStep = project.getContentSha256() == beforeHash;

    // Duplicate must land one selection span later, rounded up to the snap grid.
    pianoRoll->setSelectedNotes (chosen);
    auto sourceEarliest = notes[0].beat;
    auto sourceLatestEnd = notes[0].beat + notes[0].lengthBeats;
    for (const auto& id : chosen)
    {
        const auto note = project.findNote (id);
        sourceEarliest = juce::jmin (sourceEarliest, note->beat);
        sourceLatestEnd = juce::jmax (sourceLatestEnd, note->beat + note->lengthBeats);
    }
    const auto snapBeats = project.getSnapBeats();
    const auto expectedOffset = juce::jmax (
        snapBeats,
        std::ceil ((sourceLatestEnd - sourceEarliest) / snapBeats - 1.0e-9) * snapBeats);
    const auto duplicated = pianoRoll->duplicateSelection().wasOk();
    auto duplicateLandedOnGrid = duplicated && pianoRoll->getSelectedNotes().size() == 3;
    for (const auto& id : pianoRoll->getSelectedNotes())
    {
        const auto note = project.findNote (id);
        duplicateLandedOnGrid = duplicateLandedOnGrid && note.has_value()
                                && note->beat >= sourceEarliest + expectedOffset - 1.0e-6;
    }
    performUndoRedo (false);
    const auto duplicateUndoneInOneStep = project.getContentSha256() == beforeHash;

    // Arrow-key edits move the whole selection and stay one Undo step each.
    pianoRoll->setSelectedNotes (chosen);
    pianoRoll->transposeSelection (12);
    auto octaveIterator = originalPitches.begin();
    auto allRaisedAnOctave = true;
    for (const auto& id : chosen)
    {
        const auto note = project.findNote (id);
        allRaisedAnOctave = allRaisedAnOctave && note.has_value()
                            && note->midiNote == *octaveIterator + 12;
        ++octaveIterator;
    }
    performUndoRedo (false);
    pianoRoll->setSelectedNotes (chosen);
    pianoRoll->nudgeSelection (project.getSnapBeats());
    const auto nudgedForward = std::all_of (chosen.begin(),
                                            chosen.end(),
                                            [this] (const juce::String& id)
                                            {
                                                return project.findNote (id).has_value();
                                            })
                               && project.getContentSha256() != beforeHash;
    performUndoRedo (false);
    const auto keyboardEditsUndone = project.getContentSha256() == beforeHash;

    pianoRoll->setSelectedNote ({});
    const auto clearedSelection = pianoRoll->getSelectedNotes().empty()
                                  && pianoRoll->getSelectedNote().isEmpty();

    clearEditPreview (true);
    project.markClean();

    const auto passed = selectedThree && rejectedUnknownAndDuplicate && allVelocitiesSet
                        && velocityUndoneInOneStep && transposePreviewCreated
                        && transposeDiffCount == 3 && activeUnchangedDuringPreview
                        && allTransposed && transposeUndoneInOneStep && prunedAfterRemoval
                        && copiedToClipboard && pasteAddedNewNotes && pasteUndoneInOneStep
                        && duplicateLandedOnGrid && duplicateUndoneInOneStep
                        && allRaisedAnOctave && nudgedForward && keyboardEditsUndone
                        && clearedSelection;

    resultObject->setProperty ("selectedThree", selectedThree);
    resultObject->setProperty ("rejectedUnknownAndDuplicate", rejectedUnknownAndDuplicate);
    resultObject->setProperty ("velocityAppliedAcrossSelection", allVelocitiesSet);
    resultObject->setProperty ("velocityUndoneInOneStep", velocityUndoneInOneStep);
    resultObject->setProperty ("transposePreviewCreated", transposePreviewCreated);
    resultObject->setProperty ("transposeDiffCount", transposeDiffCount);
    resultObject->setProperty ("activeUnchangedDuringPreview", activeUnchangedDuringPreview);
    resultObject->setProperty ("transposeAppliedAcrossSelection", allTransposed);
    resultObject->setProperty ("transposeUndoneInOneStep", transposeUndoneInOneStep);
    resultObject->setProperty ("prunedAfterRemoval", prunedAfterRemoval);
    resultObject->setProperty ("copiedToClipboard", copiedToClipboard);
    resultObject->setProperty ("pasteAddedNewNotes", pasteAddedNewNotes);
    resultObject->setProperty ("pasteUndoneInOneStep", pasteUndoneInOneStep);
    resultObject->setProperty ("duplicateLandedOnGrid", duplicateLandedOnGrid);
    resultObject->setProperty ("duplicateUndoneInOneStep", duplicateUndoneInOneStep);
    resultObject->setProperty ("octaveTransposeApplied", allRaisedAnOctave);
    resultObject->setProperty ("nudgeApplied", nudgedForward);
    resultObject->setProperty ("keyboardEditsUndone", keyboardEditsUndone);
    resultObject->setProperty ("clearedSelection", clearedSelection);
    resultObject->setProperty ("passed", passed);
    if (! passed)
        resultObject->setProperty ("error", "One or more selection expectations failed");

    return result;
}

juce::var MainEditorComponent::runSoundShelfSelfTest (const juce::File& alternateProjectFile)
{
    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);
    resultObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    resultObject->setProperty ("shelfSchemaVersion", SoundShelf::supportedSchemaVersion);

    clearEditPreview (true);
    clearSoundCandidate();
    project.markClean();

    if (getActivePlugin() == nullptr)
    {
        resultObject->setProperty ("passed", false);
        resultObject->setProperty ("error", "The active track has no instrument");
        return result;
    }

    // Never touch the user's real shelf; this test owns a temporary file.
    const auto realShelfPath = soundShelfPath;
    soundShelfPath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("resonance-shelf-selftest", ".json", false);
    soundShelf.clear();

    // A genuinely different accepted Surge state, so "two sounds" is not a fiction.
    SongProject alternate;
    const auto alternateLoaded = alternate.loadFromFile (alternateProjectFile);
    PluginSoundSnapshot alternateSound;
    const auto alternateRead = alternateLoaded.wasOk()
                               && alternate.getPluginSoundSnapshot (alternateSound).wasOk();

    PluginSoundSnapshot acceptedBefore;
    const auto acceptedRead = project.getPluginSoundSnapshot (acceptedBefore).wasOk();
    const auto acceptedHashBefore = project.getPluginStateSha256();
    const auto contentBefore = project.getContentSha256();
    const auto soundsDiffer = alternateRead && acceptedRead
                              && ! alternateSound.stateSha256.equalsIgnoreCase (
                                     acceptedBefore.stateSha256);

    soundNameEditor.setText ("Shelf test A", false);
    saveSoundToShelf();
    const auto savedAccepted = soundShelf.getEntryCount() == 1
                               && soundShelf.find ("Shelf test A") != nullptr
                               && soundShelfPath.existsAsFile();

    SoundShelfEntry alternateEntry;
    alternateEntry.name = "Shelf test B";
    alternateEntry.pluginIdentifier = pluginRecord.identifier;
    alternateEntry.pluginName = pluginRecord.description.name;
    alternateEntry.vendor = pluginRecord.description.manufacturerName;
    alternateEntry.version = pluginRecord.description.version;
    alternateEntry.state = alternateSound.state;
    alternateEntry.stateSha256 = alternateSound.stateSha256;
    const auto addedAlternate = alternateRead && soundShelf.add (alternateEntry).wasOk()
                                && soundShelf.saveTo (soundShelfPath).wasOk();

    SoundShelf reloaded;
    const auto shelfSurvivesReload = reloaded.loadFrom (soundShelfPath).wasOk()
                                     && reloaded.getEntryCount() == 2
                                     && reloaded.find ("Shelf test B") != nullptr
                                     && reloaded.find ("Shelf test B")->state == alternateSound.state;

    // Loading must produce candidate B and leave the accepted sound untouched.
    shelfCombo.setText ("Shelf test B", juce::dontSendNotification);
    const auto loadedAsCandidate = loadSoundFromShelf (false).wasOk()
                                   && soundCandidate.has_value()
                                   && soundCandidate->stateSha256.equalsIgnoreCase (
                                          alternateSound.stateSha256);
    const auto acceptedUnchangedByLoad = project.getPluginStateSha256() == acceptedHashBefore
                                         && project.getContentSha256() == contentBefore
                                         && ! project.isDirty();

    rejectSoundCandidate();
    const auto rejectRestoredAccepted = ! soundCandidate.has_value()
                                        && project.getPluginStateSha256() == acceptedHashBefore
                                        && ! project.isDirty();

    shelfCombo.setText ("Shelf test B", juce::dontSendNotification);
    const auto reloadedForApply = loadSoundFromShelf (false).wasOk();
    applySoundCandidate();
    const auto appliedShelfSound = reloadedForApply && ! soundCandidate.has_value()
                                   && project.getPluginStateSha256().equalsIgnoreCase (
                                          alternateSound.stateSha256)
                                   && project.isDirty();
    performUndoRedo (false);
    const auto undoRestoredAccepted = project.getPluginStateSha256() == acceptedHashBefore
                                      && project.getContentSha256() == contentBefore;

    // A shelf sound captured from a different plug-in must fail closed.
    SoundShelfEntry foreign = alternateEntry;
    foreign.name = "Shelf test foreign";
    foreign.pluginIdentifier = "VST3-Some Other Synth-00000000-00000000";
    foreign.pluginName = "Some Other Synth";
    const auto foreignAdded = soundShelf.add (foreign).wasOk();
    shelfCombo.setText ("Shelf test foreign", juce::dontSendNotification);
    const auto foreignRefused = foreignAdded && loadSoundFromShelf (false).failed()
                                && ! soundCandidate.has_value()
                                && project.getPluginStateSha256() == acceptedHashBefore;

    soundShelfPath.deleteFile();
    soundShelfPath = realShelfPath;
    soundShelf.clear();
    soundShelf.loadFrom (soundShelfPath);
    clearSoundCandidate();
    clearEditPreview (true);
    project.markClean();
    refreshProjectControls();

    const auto passed = soundsDiffer && savedAccepted && addedAlternate && shelfSurvivesReload
                        && loadedAsCandidate && acceptedUnchangedByLoad && rejectRestoredAccepted
                        && appliedShelfSound && undoRestoredAccepted && foreignRefused;

    resultObject->setProperty ("acceptedSoundSha256", acceptedBefore.stateSha256);
    resultObject->setProperty ("shelfSoundSha256", alternateSound.stateSha256);
    resultObject->setProperty ("soundsDiffer", soundsDiffer);
    resultObject->setProperty ("savedAcceptedToShelf", savedAccepted);
    resultObject->setProperty ("addedSecondSound", addedAlternate);
    resultObject->setProperty ("shelfSurvivesReload", shelfSurvivesReload);
    resultObject->setProperty ("loadedAsCandidate", loadedAsCandidate);
    resultObject->setProperty ("acceptedUnchangedByLoad", acceptedUnchangedByLoad);
    resultObject->setProperty ("rejectRestoredAccepted", rejectRestoredAccepted);
    resultObject->setProperty ("appliedShelfSound", appliedShelfSound);
    resultObject->setProperty ("undoRestoredAccepted", undoRestoredAccepted);
    resultObject->setProperty ("foreignInstrumentRefused", foreignRefused);
    resultObject->setProperty ("passed", passed);
    if (! passed)
        resultObject->setProperty ("error", "One or more sound shelf expectations failed");

    return result;
}

juce::var MainEditorComponent::runAudioProbeSelfTest (const juce::File& projectFile)
{
    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);
    resultObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    resultObject->setProperty ("projectPath", projectFile.getFileName());

    const auto fail = [resultObject, &result] (const juce::String& message)
    {
        resultObject->setProperty ("passed", false);
        resultObject->setProperty ("error", message);
        return result;
    };

    if (projectFile != juce::File() && ! openProjectForSnapshot (projectFile))
        return fail ("The probe project could not be opened");

    const auto trackCount = project.getTrackCount();
    resultObject->setProperty ("trackCount", trackCount);
    if (trackCount < 1)
        return fail ("The probe project has no tracks");

    clearEditPreview (true);
    clearSoundCandidate();
    publishProjectMixerSnapshot();

    constexpr double probeSampleRate = 44100.0;
    constexpr int probeBlockSize = 441;
    const auto prepared = engine.prepareForOfflineRender (probeSampleRate, probeBlockSize);
    if (prepared.failed())
        return fail ("Offline render preparation failed: " + prepared.getErrorMessage());

    // Master gain is forced to unity so a track's silence cannot be an artefact of the
    // session's monitoring level.
    engine.setMasterGainDecibels (0.0f);
    engine.stopAndRewind();
    engine.setPlaying (true);

    const auto loopSeconds = project.getLoopLengthBeats() * 60.0 / project.getTempoBpm();
    const auto blockCount = juce::jmax (64,
                                        static_cast<int> (std::ceil (probeSampleRate * loopSeconds
                                                                     / probeBlockSize)));

    juce::AudioBuffer<float> output (2, probeBlockSize);
    juce::AudioIODeviceCallbackContext callbackContext;
    std::array<float, SongProject::maxProjectTracks> trackPeaks {};
    float masterPeak = 0.0f;

    for (int block = 0; block < blockCount; ++block)
    {
        output.clear();
        auto* outputs = output.getArrayOfWritePointers();
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, outputs, 2, probeBlockSize,
                                                 callbackContext);
        masterPeak = juce::jmax (masterPeak, output.getMagnitude (0, probeBlockSize));
        for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            const auto slot = static_cast<std::size_t> (trackIndex);
            trackPeaks[slot] = juce::jmax (trackPeaks[slot],
                                           juce::jmax (engine.getTrackLeftPeak (slot),
                                                       engine.getTrackRightPeak (slot)));
        }
    }

    engine.setPlaying (false);
    engine.stopAndRewind();

    // A muted track, or any track while another is soloed, is expected to be silent;
    // only tracks that should be heard are required to produce signal.
    auto anySolo = false;
    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        anySolo = anySolo || project.getTrackMixerSettings (trackIndex).solo;

    juce::Array<juce::var> trackReports;
    auto audibleTracks = 0;
    auto expectedAudibleTracks = 0;
    auto expectationsMet = true;
    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
    {
        const auto slot = static_cast<std::size_t> (trackIndex);
        const auto peak = trackPeaks[slot];
        const auto settings = project.getTrackMixerSettings (trackIndex);
        const auto shouldSound = ! settings.muted && (! anySolo || settings.solo);
        // -60 dBFS is the project's own lower mixer bound, so anything below it is
        // treated as silence rather than a quiet part.
        const auto audible = peak > 0.001f;
        audibleTracks += audible ? 1 : 0;
        expectedAudibleTracks += shouldSound ? 1 : 0;
        expectationsMet = expectationsMet && (audible == shouldSound);

        auto* trackObject = new juce::DynamicObject();
        juce::var trackReport (trackObject);
        trackObject->setProperty ("index", trackIndex);
        trackObject->setProperty ("name", project.getTrackName (trackIndex));
        trackObject->setProperty ("noteCount",
                                  static_cast<int> (project.getNotes (trackIndex).size()));
        trackObject->setProperty ("midiOutputChannel",
                                  project.getTrackMidiRouting (trackIndex).outputChannel);
        trackObject->setProperty ("gainDb", project.getTrackMixerSettings (trackIndex).gainDecibels);
        trackObject->setProperty ("peak", peak);
        trackObject->setProperty ("processedBlocks",
                                  static_cast<int> (engine.getTrackProcessedBlockCount (slot)));
        trackObject->setProperty ("muted", settings.muted);
        trackObject->setProperty ("shouldSound", shouldSound);
        trackObject->setProperty ("audible", audible);
        trackReports.add (trackReport);
    }

    resultObject->setProperty ("blockCount", blockCount);
    resultObject->setProperty ("tracks", trackReports);
    resultObject->setProperty ("audibleTrackCount", audibleTracks);
    resultObject->setProperty ("expectedAudibleTrackCount", expectedAudibleTracks);
    resultObject->setProperty ("masterPeak", masterPeak);
    resultObject->setProperty ("clipped", masterPeak > 1.0f);
    resultObject->setProperty ("invalidSampleCount",
                               static_cast<int> (engine.getInvalidSampleCount()));

    const auto passed = expectationsMet && masterPeak > 0.001f && masterPeak <= 1.0f
                        && engine.getInvalidSampleCount() == 0;
    resultObject->setProperty ("passed", passed);
    if (! passed)
        resultObject->setProperty ("error",
                                   juce::String (audibleTracks) + " of "
                                       + juce::String (expectedAudibleTracks)
                                       + " expected-audible tracks produced signal");

    return result;
}

bool MainEditorComponent::openProjectForSnapshot (const juce::File& projectFile)
{
    if (! projectFile.existsAsFile())
        return false;

    openProjectFile (projectFile);
    return currentProjectFile == projectFile;
}

juce::var MainEditorComponent::runCommandLoadSelfTest()
{
    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);
    resultObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    resultObject->setProperty ("commandVersion", EditCommand::supportedVersion);

    clearEditPreview (true);
    clearSoundCandidate();
    project.markClean();

    if (getActivePlugin() == nullptr || pianoRoll == nullptr || project.getNotes().empty())
    {
        resultObject->setProperty ("passed", false);
        resultObject->setProperty ("error", "The starter project has no instrument or editable note");
        return result;
    }

    const auto original = project.getNotes().front();
    const auto beforeHash = project.getContentSha256();
    const auto trackId = project.getTrackId();
    const auto clipId = project.getClipId();
    resultObject->setProperty ("projectContentSha256", beforeHash);
    resultObject->setProperty ("targetTrackId", trackId);
    resultObject->setProperty ("targetClipId", clipId);

    // One velocity update the resolver would never produce on its own, so a passing
    // preview proves the file drove it rather than the seeded dynamics path.
    auto updated = original;
    updated.velocity = original.velocity >= 64 ? original.velocity - 21 : original.velocity + 21;

    const auto buildCommand = [&] (const juce::String& hash,
                                   const juce::String& targetTrack,
                                   const juce::String& targetClip)
    {
        EditCommand command;
        command.projectContentSha256 = hash;
        command.trackId = targetTrack;
        command.clipId = targetClip;
        command.summary = "Command load self test";
        NoteEditChange change;
        change.action = NoteEditAction::update;
        change.noteId = original.id;
        change.note = updated;
        command.changes.push_back (change);
        return command;
    };

    const auto tempDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory);
    std::vector<juce::File> temporaryFiles;
    const auto writeCommandFile = [&] (const juce::String& contents)
    {
        const auto file = tempDirectory.getNonexistentChildFile ("resonance-command-load",
                                                                 ".json",
                                                                 false);
        file.replaceWithText (contents);
        temporaryFiles.push_back (file);
        return file;
    };

    // Every refusal below must leave the active project byte-identical and preview-free.
    // Failures are loaded silently so the headless run never stacks modal alerts.
    const auto refusedCleanly = [&] (const juce::File& file)
    {
        return loadEditCommandFile (file, false).failed()
               && ! hasPendingEditPreview()
               && project.getContentSha256() == beforeHash
               && ! project.isDirty();
    };

    const auto staleHash = juce::String::repeatedString ("0", 64);
    const auto staleRefused = refusedCleanly (
        writeCommandFile (serialiseEditCommand (buildCommand (staleHash, trackId, clipId))));
    const auto wrongTrackRefused = refusedCleanly (
        writeCommandFile (serialiseEditCommand (
            buildCommand (beforeHash, trackId + "-not-a-track", clipId))));
    const auto wrongClipRefused = refusedCleanly (
        writeCommandFile (serialiseEditCommand (
            buildCommand (beforeHash, trackId, clipId + "-not-a-clip"))));
    const auto malformedRefused = refusedCleanly (writeCommandFile ("{ this is not json"));
    const auto oversizeRefused = refusedCleanly (
        writeCommandFile (juce::String::repeatedString (
            "x", static_cast<int> (maximumEditCommandBytes) + 1)));
    const auto missingFile = tempDirectory.getNonexistentChildFile ("resonance-command-absent",
                                                                    ".json",
                                                                    false);
    const auto missingRefused = refusedCleanly (missingFile);

    const auto validFile = writeCommandFile (
        serialiseEditCommand (buildCommand (beforeHash, trackId, clipId)));
    const auto validLoaded = loadEditCommandFile (validFile, false).wasOk();
    const auto previewCreated = validLoaded && hasPendingEditPreview();
    const auto diffCount = previewCreated ? static_cast<int> (editPreview->noteDiffs.size()) : 0;
    const auto candidate = previewCreated ? editPreview->getCandidateProject() : nullptr;
    const auto candidateNote = candidate != nullptr ? candidate->findNote (original.id)
                                                    : std::nullopt;
    const auto candidateCarriesEdit = candidateNote.has_value()
                                      && candidateNote->velocity == updated.velocity;
    const auto activeUnchangedDuringPreview = project.getContentSha256() == beforeHash
                                              && ! project.isDirty();
    const auto candidateHash = previewCreated ? editPreview->afterContentSha256 : juce::String {};
    // The load lane must interlock with the sound lane exactly like the resolver lane does.
    const auto soundLaneInterlocked = previewCreated
                                      && ! captureSoundButton.isEnabled()
                                      && ! applySoundButton.isEnabled()
                                      && ! loadCommandButton.isEnabled();

    applyEditPreview();
    const auto appliedNote = project.findNote (original.id);
    const auto appliedHash = project.getContentSha256();
    const auto applied = ! hasPendingEditPreview()
                         && appliedNote.has_value()
                         && appliedNote->velocity == updated.velocity
                         && project.isDirty()
                         && appliedHash != beforeHash;

    // The precondition is the pre-edit content hash, so replaying the same file while
    // the edit still stands must fail closed instead of stacking a second edit. After
    // Undo the hash legitimately returns to beforeHash and the file is valid again.
    const auto replayAfterApplyRefused = loadEditCommandFile (validFile, false).failed()
                                         && ! hasPendingEditPreview()
                                         && project.getContentSha256() == appliedHash;

    performUndoRedo (false);
    const auto undoneNote = project.findNote (original.id);
    const auto undoneInOneStep = undoneNote.has_value()
                                 && undoneNote->velocity == original.velocity
                                 && project.getContentSha256() == beforeHash;

    for (const auto& file : temporaryFiles)
        file.deleteFile();

    clearEditPreview (true);
    project.markClean();

    const auto passed = staleRefused && wrongTrackRefused && wrongClipRefused
                        && malformedRefused && oversizeRefused && missingRefused
                        && previewCreated && diffCount == 1 && candidateCarriesEdit
                        && activeUnchangedDuringPreview && soundLaneInterlocked
                        && applied && replayAfterApplyRefused && undoneInOneStep;

    resultObject->setProperty ("staleHashRefused", staleRefused);
    resultObject->setProperty ("wrongTrackRefused", wrongTrackRefused);
    resultObject->setProperty ("wrongClipRefused", wrongClipRefused);
    resultObject->setProperty ("malformedRefused", malformedRefused);
    resultObject->setProperty ("oversizeRefused", oversizeRefused);
    resultObject->setProperty ("missingFileRefused", missingRefused);
    resultObject->setProperty ("previewCreated", previewCreated);
    resultObject->setProperty ("noteDiffCount", diffCount);
    resultObject->setProperty ("candidateCarriesEdit", candidateCarriesEdit);
    resultObject->setProperty ("activeUnchangedDuringPreview", activeUnchangedDuringPreview);
    resultObject->setProperty ("soundLaneInterlocked", soundLaneInterlocked);
    resultObject->setProperty ("candidateContentSha256", candidateHash);
    resultObject->setProperty ("appliedAsOneTransaction", applied);
    resultObject->setProperty ("replayAfterApplyRefused", replayAfterApplyRefused);
    resultObject->setProperty ("undoneInOneStep", undoneInOneStep);
    resultObject->setProperty ("passed", passed);
    if (! passed)
        resultObject->setProperty ("error", "One or more command-load expectations failed");

    return result;
}

void MainEditorComponent::chooseEditCommandFile()
{
    if (soundCandidate.has_value())
    {
        projectStatusMessage = "APPLY OR REJECT SOUND B BEFORE LOADING A COMMAND";
        updateStatus();
        return;
    }

    projectStatusMessage = "CHOOSE AN EDIT COMMAND TO PREVIEW";
    updateStatus();
    const auto initial = currentProjectFile != juce::File()
                             ? currentProjectFile.getParentDirectory()
                             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    activeFileChooser = std::make_unique<juce::FileChooser> ("Preview a Resonance edit command",
                                                             initial,
                                                             "*.json",
                                                             true);
    const juce::Component::SafePointer<MainEditorComponent> safe (this);
    activeFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles,
                                    [safe] (const juce::FileChooser& chooser)
                                    {
                                        if (safe == nullptr)
                                            return;

                                        if (chooser.getResult() != juce::File())
                                            safe->loadEditCommandFile (chooser.getResult());
                                        else
                                        {
                                            safe->projectStatusMessage = "COMMAND LOAD CANCELED";
                                            safe->updateStatus();
                                        }
                                    });
}

juce::Result MainEditorComponent::loadEditCommandFile (const juce::File& commandFile,
                                                       bool reportFailure)
{
    const auto refuse = [this, reportFailure] (const juce::String& status,
                                               const juce::String& detail)
    {
        projectStatusMessage = status;
        updateStatus();
        if (reportFailure)
            showError ("Could not load edit command", detail);
        return juce::Result::fail (detail);
    };

    if (soundCandidate.has_value())
        return refuse ("APPLY OR REJECT SOUND B BEFORE LOADING A COMMAND",
                       "Apply or reject the pending sound candidate first.");

    if (getActivePlugin() == nullptr)
        return refuse ("COMMAND LOAD BLOCKED  /  NO ACTIVE INSTRUMENT",
                       "The active track has no loaded instrument.");

    if (! commandFile.existsAsFile())
        return refuse ("COMMAND FILE NOT FOUND",
                       "The chosen edit-command file no longer exists.");

    if (commandFile.getSize() > maximumEditCommandBytes)
        return refuse ("COMMAND FILE TOO LARGE",
                       "An edit command must be at most 256 KB.");

    EditCommand command;
    const auto parsed = parseEditCommand (commandFile.loadFileAsString(), command);
    if (parsed.failed())
        return refuse ("COMMAND PARSE ERROR  /  " + parsed.getErrorMessage().toUpperCase(),
                       parsed.getErrorMessage());

    // installEditPreview re-checks the content hash and target ids through
    // createEditCommandPreview, so a stale or mistargeted command still fails closed.
    return installEditPreview (std::move (command),
                               "COMMAND B READY  /  " + commandFile.getFileName().toUpperCase(),
                               reportFailure);
}

void MainEditorComponent::copyProjectContentHash()
{
    const auto hash = project.getContentSha256();
    const auto trackId = project.getTrackId();
    const auto clipId = project.getClipId();
    juce::SystemClipboard::copyTextToClipboard (hash + "\n" + trackId + "\n" + clipId);
    projectStatusMessage = "COPIED HASH " + hash.substring (0, 16).toUpperCase()
                           + "  /  TRACK " + trackId + "  /  CLIP " + clipId;
    updateStatus();
}

juce::Result MainEditorComponent::installEditPreview (EditCommand command,
                                                      const juce::String& readyStatus,
                                                      bool reportFailure)
{
    if (hasPendingEditPreview())
        clearEditPreview (true);

    EditCommandPreview proposed;
    const auto result = createEditCommandPreview (command, project, proposed);
    if (result.failed())
    {
        projectStatusMessage = "EDIT PROPOSAL ERROR  /  " + result.getErrorMessage();
        updateStatus();
        if (reportFailure)
            showError ("Could not preview edit", result.getErrorMessage());
        return result;
    }

    editPreview.emplace (std::move (proposed));
    auditioningEditCandidate = false;
    publishProjectMixerSnapshot();
    if (pianoRoll != nullptr)
        pianoRoll->setEditPreview (editPreview->noteDiffs, false);
    projectStatusMessage = readyStatus;
    refreshProjectControls();
    return juce::Result::ok();
}

void MainEditorComponent::auditionEditProject()
{
    if (! hasPendingEditPreview())
        return;

    publishProjectMixerSnapshot();
    auditioningEditCandidate = false;
    if (pianoRoll != nullptr)
        pianoRoll->setEditPreviewAudition (false);
    projectStatusMessage = "AUDITIONING NOTE A  /  PROJECT REMAINS UNCHANGED";
    refreshEditPreviewControls();
}

void MainEditorComponent::auditionEditCandidate()
{
    if (! hasPendingEditPreview())
        return;

    const auto* candidate = editPreview->getCandidateProject();
    if (candidate == nullptr)
        return;

    const auto candidateSequence = candidate->createSequenceSnapshot();
    publishProjectMixerSnapshot (&candidateSequence);
    auditioningEditCandidate = true;
    if (pianoRoll != nullptr)
        pianoRoll->setEditPreviewAudition (true);
    projectStatusMessage = "AUDITIONING NOTE B  /  PROJECT REMAINS UNCHANGED";
    refreshEditPreviewControls();
}

void MainEditorComponent::applyEditPreview()
{
    if (! hasPendingEditPreview())
        return;

    juce::Result result = juce::Result::ok();
    {
        const juce::ScopedValueSetter<bool> applying (applyingEditPreview, true);
        result = editPreview->applyTo (project);
    }

    if (result.failed())
    {
        publishProjectMixerSnapshot();
        auditioningEditCandidate = false;
        if (pianoRoll != nullptr)
            pianoRoll->setEditPreviewAudition (false);
        projectStatusMessage = "NOTE PROPOSAL COULD NOT BE APPLIED";
        refreshProjectControls();
        showError ("Could not apply note proposal", result.getErrorMessage());
        return;
    }

    editPreview.reset();
    auditioningEditCandidate = false;
    if (pianoRoll != nullptr)
        pianoRoll->clearEditPreview();
    projectStatusMessage = "NOTE EDIT APPLIED  /  ONE UNDO RESTORES A";
    projectChanged();
}

void MainEditorComponent::rejectEditPreview()
{
    if (! hasPendingEditPreview())
        return;

    const auto result = editPreview->reject();
    if (result.failed())
    {
        showError ("Could not reject note proposal", result.getErrorMessage());
        return;
    }

    editPreview.reset();
    auditioningEditCandidate = false;
    if (pianoRoll != nullptr)
        pianoRoll->clearEditPreview();
    publishProjectMixerSnapshot();
    projectStatusMessage = "NOTE B REJECTED  /  PROJECT A UNCHANGED";
    refreshProjectControls();
}

void MainEditorComponent::clearEditPreview (bool publishActiveSequence)
{
    editPreview.reset();
    auditioningEditCandidate = false;
    if (pianoRoll != nullptr)
        pianoRoll->clearEditPreview();
    if (publishActiveSequence)
        publishProjectMixerSnapshot();
    refreshSoundControls();
    refreshEditPreviewControls();
}

void MainEditorComponent::refreshEditPreviewControls()
{
    const auto pending = hasPendingEditPreview();
    const auto selected = pianoRoll != nullptr
                              ? project.findNote (pianoRoll->getSelectedNote())
                              : std::nullopt;
    const auto ready = getActivePlugin() != nullptr;
    SeededVelocityVariation velocityVariation;
    const auto velocitySettings = readVelocityVariationControls (velocityVariation);

    if (pending)
    {
        int additions = 0;
        int updates = 0;
        int removals = 0;
        for (const auto& diff : editPreview->noteDiffs)
        {
            additions += diff.action == NoteEditAction::add ? 1 : 0;
            updates += diff.action == NoteEditAction::update ? 1 : 0;
            removals += diff.action == NoteEditAction::remove ? 1 : 0;
        }

        editProposalSummaryLabel.setText ("B  /  " + editPreview->command.summary.toUpperCase(),
                                          juce::dontSendNotification);
        editProposalSummaryLabel.setColour (juce::Label::textColourId,
                                            auditioningEditCandidate ? secondary : textMain);

        auto counts = juce::String (additions) + " ADD  /  "
                      + juce::String (updates) + " UPDATE  /  "
                      + juce::String (removals) + " REMOVE";
        juce::String detail;
        if (! editPreview->noteDiffs.empty())
        {
            const auto& first = editPreview->noteDiffs.front();
            detail = first.noteId;
            if (first.before.has_value())
                detail += "  " + noteName (first.before->midiNote)
                          + " v" + juce::String (first.before->velocity);
            if (first.after.has_value())
                detail += (first.before.has_value() ? "  ->  " : "  +  ")
                          + noteName (first.after->midiNote)
                          + " v" + juce::String (first.after->velocity);
        }

        auto provenance = juce::String {};
        if (editPreview->command.seed.has_value())
            provenance = "SEED "
                         + juce::String::formatted ("%lld",
                                                    static_cast<long long> (*editPreview->command.seed))
                         + "  /  ";
        editProposalDiffLabel.setText (counts + "\n" + detail
                                           + "\n" + provenance
                                           + "A " + shortStateHash (editPreview->beforeContentSha256)
                                           + "  /  B " + shortStateHash (editPreview->afterContentSha256),
                                       juce::dontSendNotification);
        editProposalDiffLabel.setTooltip ("Accepted A: " + editPreview->beforeContentSha256
                                          + "\nCandidate B: " + editPreview->afterContentSha256
                                          + (editPreview->command.seed.has_value()
                                                 ? "\nSeed: " + juce::String::formatted (
                                                     "%lld",
                                                     static_cast<long long> (*editPreview->command.seed))
                                                 : juce::String {}));
    }
    else if (velocitySettings.wasOk())
    {
        const auto wholeLoop = dynamicsScopeCombo.getSelectedId() == velocityScopeWholeLoop;
        const auto targetDescription = wholeLoop
                                           ? "WHOLE LOOP  /  "
                                                 + juce::String (static_cast<int> (
                                                     velocityVariation.noteIds.size()))
                                                 + " NOTES"
                                           : "SELECTED  /  "
                                                 + velocityVariation.noteIds.front().toUpperCase();
        editProposalSummaryLabel.setText ("DYNAMICS READY  /  " + targetDescription,
                                          juce::dontSendNotification);
        editProposalSummaryLabel.setColour (juce::Label::textColourId, textMain);
        editProposalDiffLabel.setText ("MAX +/-"
                                           + juce::String (velocityVariation.maximumDelta)
                                           + "  /  SEED "
                                           + juce::String::formatted (
                                               "%lld",
                                               static_cast<long long> (velocityVariation.seed))
                                           + "\nPreview resolves concrete velocity-only B; A stays unchanged.",
                                       juce::dontSendNotification);
        editProposalDiffLabel.setTooltip ({});
    }
    else
    {
        editProposalSummaryLabel.setText ("DYNAMICS SETTINGS NEED ATTENTION",
                                          juce::dontSendNotification);
        editProposalSummaryLabel.setColour (juce::Label::textColourId, warning);
        editProposalDiffLabel.setText (velocitySettings.getErrorMessage()
                                           + "\nSelected +1 remains available for a selected note.",
                                       juce::dontSendNotification);
        editProposalDiffLabel.setTooltip ({});
    }

    const auto controlsEnabled = ready && ! pending && ! soundCandidate.has_value();
    dynamicsScopeCombo.setEnabled (controlsEnabled);
    dynamicsStrengthEditor.setEnabled (controlsEnabled);
    dynamicsSeedEditor.setEnabled (controlsEnabled);
    // Transposing is all or nothing, so the whole selection must be below the top pitch
    // and must fit one version-1 command.
    auto selectionCanTranspose = false;
    if (pianoRoll != nullptr)
    {
        const auto& selectedIds = pianoRoll->getSelectedNotes();
        selectionCanTranspose = ! selectedIds.empty()
                                && selectedIds.size() <= maximumEditCommandChanges;
        for (const auto& id : selectedIds)
        {
            const auto note = project.findNote (id);
            selectionCanTranspose = selectionCanTranspose && note.has_value()
                                    && note->midiNote < 127;
        }
    }
    previewSelectedEditButton.setEnabled (ready && ! pending && ! soundCandidate.has_value()
                                          && selectionCanTranspose);
    previewDynamicsButton.setEnabled (controlsEnabled && velocitySettings.wasOk());
    loadCommandButton.setEnabled (controlsEnabled);
    copyHashButton.setEnabled (ready);
    auditionEditProjectButton.setEnabled (ready && pending);
    auditionEditCandidateButton.setEnabled (ready && pending);
    applyEditButton.setEnabled (ready && pending);
    rejectEditButton.setEnabled (pending);
    auditionEditProjectButton.setToggleState (pending && ! auditioningEditCandidate,
                                               juce::dontSendNotification);
    auditionEditCandidateButton.setToggleState (pending && auditioningEditCandidate,
                                                 juce::dontSendNotification);
}

void MainEditorComponent::projectChanged()
{
    if (applyingEditPreview || suppressProjectChanges)
        return;

    // Undo, Redo, and command Apply can delete notes that are still selected.
    if (pianoRoll != nullptr)
        pianoRoll->pruneSelection();


    if (pluginEditorWindow != nullptr
        && (pluginEditorTrackId != project.getTrackId()
            || pluginEditorTrackIndex != project.getActiveTrackIndex()))
    {
        pluginEditorWindow.reset();
        pluginEditorTrackId.clear();
        pluginEditorTrackIndex = -1;
    }

    if (hasPendingEditPreview()
        && (editPreview->command.trackId != project.getTrackId()
            || ! project.getContentSha256().equalsIgnoreCase (editPreview->beforeContentSha256)))
    {
        editPreview.reset();
        auditioningEditCandidate = false;
        if (pianoRoll != nullptr)
            pianoRoll->clearEditPreview();
        projectStatusMessage = "NOTE PROPOSAL STALE  /  ACTIVE PROJECT CHANGED";
    }

    engine.setBpm (project.getTempoBpm());
    const auto sync = synchronisePluginSlotsFromProject();
    runtimeProjectSyncError = sync.failed() ? sync.getErrorMessage() : juce::String {};
    updateActiveSoundTracking();
    publishProjectMixerSnapshot();

    if (pianoRoll != nullptr && pianoRoll->getSelectedNote().isNotEmpty()
        && ! project.findNote (pianoRoll->getSelectedNote()).has_value())
        pianoRoll->setSelectedNote ({});

    refreshProjectControls();
    repaint (loopCardBounds);
}

void MainEditorComponent::refreshProjectControls()
{
    const juce::ScopedValueSetter<bool> refreshing (refreshingProjectControls, true);
    updateActiveSoundTracking();
    projectNameLabel.setText (project.getTitle() + (project.isDirty() ? "  *" : ""),
                              juce::dontSendNotification);
    projectNameLabel.setTooltip (currentProjectFile == juce::File()
                                     ? "Unsaved project"
                                     : currentProjectFile.getFullPathName());
    bpmSlider.setValue (project.getTempoBpm(), juce::dontSendNotification);
    snapCombo.setSelectedId (snapComboId (project.getSnapBeats()), juce::dontSendNotification);
    loopLengthCombo.setSelectedId (loopComboId (project.getLoopLengthBeats()), juce::dontSendNotification);

    trackSelector.clear (juce::dontSendNotification);
    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
        trackSelector.addItem (juce::String (trackIndex + 1) + " / "
                                   + project.getTrackName (trackIndex),
                               trackIndex + 1);
    const auto activeTrack = project.getActiveTrackIndex();
    trackSelector.setSelectedId (activeTrack + 1, juce::dontSendNotification);
    trackNameLabel.setText (juce::String (activeTrack + 1).paddedLeft ('0', 2)
                                + "  /  " + project.getTrackName(),
                            juce::dontSendNotification);
    trackMetaLabel.setText (pluginRecord.description.manufacturerName + "  /  VST3 "
                            + pluginRecord.description.version + "  /  "
                            + juce::String (pluginRecord.expectedParameterCount) + " parameters  /  SOUND  "
                            + project.getPluginSoundName(),
                            juce::dontSendNotification);

    const auto mixerSettings = project.getTrackMixerSettings();
    trackGainSlider.setValue (mixerSettings.gainDecibels, juce::dontSendNotification);
    trackPanSlider.setValue (mixerSettings.pan, juce::dontSendNotification);
    trackMuteButton.setToggleState (mixerSettings.muted, juce::dontSendNotification);
    trackSoloButton.setToggleState (mixerSettings.solo, juce::dontSendNotification);

    const auto trackLaneClear = ! soundCandidate.has_value() && ! hasPendingEditPreview();
    const auto trackReady = getActivePlugin() != nullptr;
    trackSelector.setEnabled (trackLaneClear && project.getTrackCount() > 1);
    addTrackButton.setEnabled (trackLaneClear
                               && project.getTrackCount() < SongProject::maxProjectTracks);
    removeTrackButton.setEnabled (trackLaneClear && project.getTrackCount() > 1);
    moveTrackLeftButton.setEnabled (trackLaneClear && activeTrack > 0);
    moveTrackRightButton.setEnabled (trackLaneClear
                                     && activeTrack + 1 < project.getTrackCount());
    trackGainSlider.setEnabled (trackReady && trackLaneClear);
    trackPanSlider.setEnabled (trackReady && trackLaneClear);
    trackMuteButton.setEnabled (trackReady && trackLaneClear);
    trackSoloButton.setEnabled (trackReady && trackLaneClear);

    undoButton.setEnabled (project.canUndo());
    redoButton.setEnabled (project.canRedo());
    undoButton.setTooltip (project.canUndo() ? "Undo " + project.getUndoDescription() : "Nothing to undo");
    redoButton.setTooltip (project.canRedo() ? "Redo " + project.getRedoDescription() : "Nothing to redo");

    auto selected = pianoRoll != nullptr ? project.findNote (pianoRoll->getSelectedNote()) : std::nullopt;
    const auto selectionCount = pianoRoll != nullptr
                                    ? static_cast<int> (pianoRoll->getSelectedNotes().size())
                                    : 0;
    velocitySlider.setEnabled (selected.has_value());
    velocityLabel.setText (selectionCount > 1
                               ? juce::String (selectionCount) + " NOTES"
                               : selected.has_value() ? "VELOCITY" : "SELECT NOTE",
                           juce::dontSendNotification);
    // The slider shows the primary selection's velocity and writes one absolute value
    // across the whole selection.
    if (selected.has_value())
        velocitySlider.setValue (selected->velocity, juce::dontSendNotification);

    refreshSoundControls();
    refreshShelfControls();
    refreshEditPreviewControls();
    if (pianoRoll != nullptr)
        pianoRoll->repaint();
}

juce::String MainEditorComponent::velocityTransactionName() const
{
    const auto count = pianoRoll != nullptr ? pianoRoll->getSelectedNotes().size() : 0;
    return count > 1 ? "Change note velocities" : "Change note velocity";
}

void MainEditorComponent::selectedNoteChanged (const juce::String&)
{
    refreshProjectControls();
}

void MainEditorComponent::startNewProject()
{
    engine.stopAndRewind();
    pluginEditorWindow.reset();
    pluginEditorTrackId.clear();
    pluginEditorTrackIndex = -1;
    clearEditPreview (false);
    clearSoundCandidate();

    for (int trackIndex = 0; trackIndex < SongProject::maxProjectTracks; ++trackIndex)
    {
        if (initialPluginStates[trackIndex].getSize() == 0)
            continue;

        juce::MemoryBlock liveState;
        const auto restore = engine.restorePluginStateForTrack (
            static_cast<std::size_t> (trackIndex),
            initialPluginStates[trackIndex],
            &liveState);
        if (restore.failed())
        {
            showError ("Could not reset Surge XT", restore.getErrorMessage());
            return;
        }

        slotAcceptedLiveSoundSha256[trackIndex] = juce::SHA256 (liveState).toHexString();
    }

    {
        const juce::ScopedValueSetter<bool> suppress (suppressProjectChanges, true);
        project.resetToStarter();
        project.setPluginMetadata (pluginRecord.identifier,
                                   pluginRecord.description.name,
                                   pluginRecord.description.manufacturerName,
                                   pluginRecord.description.version);
        if (initialPluginStates[0].getSize() > 0)
            project.setPluginState (initialPluginStates[0]);
    }

    for (auto& hash : slotProjectStateSha256)
        hash.clear();
    slotProjectStateSha256[0] = project.getPluginStateSha256 (0);
    activeSoundTrackId = project.getTrackId();
    acceptedLiveSoundSha256 = slotAcceptedLiveSoundSha256[0];
    auditionedSoundSha256 = acceptedLiveSoundSha256;
    currentProjectFile = {};
    if (pianoRoll != nullptr)
    {
        pianoRoll->setSelectedNote ({});
        pianoRoll->frameAllTracks();
    }
    projectStatusMessage = "NEW PROJECT  /  TRACK 1 READY";
    projectChanged();
}

void MainEditorComponent::chooseProjectToOpen()
{
    projectStatusMessage = "CHOOSE A SONG PROJECT TO OPEN";
    updateStatus();
    const auto initial = currentProjectFile != juce::File()
                             ? currentProjectFile
                             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    activeFileChooser = std::make_unique<juce::FileChooser> ("Open Resonance song",
                                                             initial,
                                                             "*.resonance.json;*.json",
                                                             true);
    const juce::Component::SafePointer<MainEditorComponent> safe (this);
    activeFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles,
                                    [safe] (const juce::FileChooser& chooser)
                                    {
                                        if (safe == nullptr)
                                            return;

                                        if (chooser.getResult() != juce::File())
                                            safe->openProjectFile (chooser.getResult());
                                        else
                                        {
                                            safe->projectStatusMessage = "OPEN CANCELED";
                                            safe->updateStatus();
                                        }
                                    });
}

void MainEditorComponent::openProjectFile (const juce::File& file)
{
    SongProject candidate;
    const auto loadResult = candidate.loadFromFile (file);
    if (loadResult.failed())
    {
        showError ("Could not open song", loadResult.getErrorMessage());
        return;
    }

    for (int trackIndex = 0; trackIndex < candidate.getTrackCount(); ++trackIndex)
    {
        const auto identifierMatches = vst3IdentifiersAreCompatible (
            candidate.getPluginIdentifier (trackIndex),
            pluginRecord.identifier,
            pluginRecord.description.uniqueId);
        const auto nameMatches = candidate.getPluginName (trackIndex).equalsIgnoreCase (
            pluginRecord.description.name);
        if (! identifierMatches || ! nameMatches)
        {
            showError ("Different instrument",
                       "Track " + juce::String (trackIndex + 1)
                           + " does not match the accepted instrument. Saved: "
                           + candidate.getPluginIdentifier (trackIndex)
                           + ". Active: " + pluginRecord.identifier + ".");
            return;
        }

        juce::MemoryBlock state;
        const auto stateResult = candidate.getPluginStateForTrack (trackIndex, state);
        if (stateResult.failed())
        {
            showError ("Invalid Surge state",
                       "Track " + juce::String (trackIndex + 1) + ": "
                           + stateResult.getErrorMessage());
            return;
        }
    }

    engine.stopAndRewind();
    pluginEditorWindow.reset();
    pluginEditorTrackId.clear();
    pluginEditorTrackIndex = -1;
    clearEditPreview (false);
    clearSoundCandidate();
    auto liveStateHashes = slotAcceptedLiveSoundSha256;
    const auto restoreResult = restoreRuntimeSlotsFromProject (candidate, liveStateHashes);
    if (restoreResult.failed())
    {
        showError ("Could not restore Surge XT", restoreResult.getErrorMessage());
        return;
    }

    {
        const juce::ScopedValueSetter<bool> suppress (suppressProjectChanges, true);
        project.replaceWith (candidate);
    }
    currentProjectFile = file;
    project.markClean();
    slotAcceptedLiveSoundSha256 = liveStateHashes;
    for (int trackIndex = 0; trackIndex < SongProject::maxProjectTracks; ++trackIndex)
        slotProjectStateSha256[trackIndex] = trackIndex < project.getTrackCount()
                                                        ? project.getPluginStateSha256 (trackIndex)
                                                        : juce::String {};
    activeSoundTrackId = project.getTrackId();
    acceptedLiveSoundSha256 = slotAcceptedLiveSoundSha256[project.getActiveTrackIndex()];
    auditionedSoundSha256 = acceptedLiveSoundSha256;
    projectStatusMessage = "OPENED  /  " + file.getFileName();
    if (pianoRoll != nullptr)
    {
        pianoRoll->setSelectedNote ({});
        // Fit the pitch range of every track so both parts are visible on open
        // rather than leaving material above or below the default window.
        pianoRoll->frameAllTracks();
    }
    projectChanged();
}

void MainEditorComponent::saveProject()
{
    projectStatusMessage = (soundCandidate.has_value() || hasPendingEditPreview())
                               ? "SAVING A  /  PREVIEW B REMAINS UNAPPLIED"
                               : "SAVING THE ACCEPTED PROJECT";
    updateStatus();
    if (currentProjectFile == juce::File())
        chooseProjectSaveLocation();
    else
        saveProjectToFile (currentProjectFile);
}

void MainEditorComponent::chooseProjectSaveLocation()
{
    projectStatusMessage = "CHOOSE WHERE TO SAVE THE SONG";
    updateStatus();
    const auto initial = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                             .getChildFile (project.getTitle().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ")
                                                .trim()
                                            + ".resonance.json");
    activeFileChooser = std::make_unique<juce::FileChooser> ("Save Resonance song",
                                                             initial,
                                                             "*.resonance.json;*.json",
                                                             true);
    const juce::Component::SafePointer<MainEditorComponent> safe (this);
    activeFileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [safe] (const juce::FileChooser& chooser)
                                    {
                                        if (safe == nullptr)
                                            return;

                                        if (chooser.getResult() != juce::File())
                                            safe->saveProjectToFile (chooser.getResult());
                                        else
                                        {
                                            safe->projectStatusMessage = "SAVE CANCELED";
                                            safe->updateStatus();
                                        }
                                    });
}

void MainEditorComponent::saveProjectToFile (juce::File file)
{
    if (! file.getFileName().endsWithIgnoreCase (".json"))
        file = file.getSiblingFile (file.getFileName() + ".resonance.json");

    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
    {
        PluginSoundSnapshot acceptedSound;
        const auto stateResult = project.getPluginSoundSnapshotForTrack (trackIndex,
                                                                         acceptedSound);
        if (stateResult.failed())
        {
            showError ("Could not read the accepted project sound",
                       "Track " + juce::String (trackIndex + 1) + ": "
                           + stateResult.getErrorMessage());
            return;
        }

        project.setPluginMetadataForTrack (trackIndex,
                                           pluginRecord.identifier,
                                           pluginRecord.description.name,
                                           pluginRecord.description.manufacturerName,
                                           pluginRecord.description.version);
    }
    const auto rate = juce::roundToInt (engine.getSampleRate());
    if (rate == 44100 || rate == 48000 || rate == 88200 || rate == 96000)
        project.setSampleRate (rate);

    const auto saveResult = project.saveToFile (file);
    if (saveResult.failed())
    {
        showError ("Could not save song", saveResult.getErrorMessage());
        return;
    }

    currentProjectFile = file;
    project.markClean();
    projectStatusMessage = "SAVED  /  " + file.getFileName()
                           + ((soundCandidate.has_value() || hasPendingEditPreview())
                                  ? "  /  PREVIEW B NOT APPLIED"
                                  : "");
    refreshProjectControls();
}

void MainEditorComponent::confirmDiscardIfNeeded (std::function<void()> action)
{
    const auto hasLiveSoundChanges = hasUncapturedLiveSoundState();
    if (! project.isDirty() && ! soundCandidate.has_value()
        && ! hasPendingEditPreview() && ! hasLiveSoundChanges)
    {
        action();
        return;
    }

    juce::StringArray pending;
    if (project.isDirty())
        pending.add ("unsaved song edits");
    if (soundCandidate.has_value())
        pending.add ("unapplied sound B");
    if (hasPendingEditPreview())
        pending.add ("unapplied note proposal B");
    if (hasLiveSoundChanges)
        pending.add ("uncaptured live Surge changes");
    const auto message = "Discard " + pending.joinIntoString (", ") + "?";

    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::WarningIcon)
                             .withTitle ("Unsaved song changes")
                             .withMessage (message)
                             .withButton ("Discard")
                             .withButton ("Cancel")
                             .withAssociatedComponent (this);
    const juce::Component::SafePointer<MainEditorComponent> safe (this);
    juce::AlertWindow::showAsync (options, [safe, action = std::move (action)] (int result)
    {
        if (safe != nullptr && result == 1)
            action();
    });
}

void MainEditorComponent::requestClose (std::function<void()> closeAction)
{
    confirmDiscardIfNeeded (std::move (closeAction));
}

juce::var MainEditorComponent::runM4WorkflowSelfTest (const juce::File& projectFile)
{
    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);
    resultObject->setProperty ("projectPath", projectFile.getFullPathName());

    openProjectFile (projectFile);

    const auto opened = currentProjectFile == projectFile && ! project.isDirty();
    juce::MemoryBlock baselineLiveState;
    const auto baselineCapture = engine.capturePluginStateForTrack (
        static_cast<std::size_t> (project.getActiveTrackIndex()), baselineLiveState);
    const auto baselineLiveHash = baselineCapture.wasOk()
                                      ? juce::SHA256 (baselineLiveState).toHexString()
                                      : juce::String {};
    const auto savedHash = project.getPluginStateSha256();
    const auto acceptedHashBeforePlay = acceptedLiveSoundSha256;
    const auto baselineMatchesAccepted = baselineLiveHash.isNotEmpty()
                                         && baselineLiveHash.equalsIgnoreCase (acceptedHashBeforePlay);

    resultObject->setProperty ("opened", opened);
    resultObject->setProperty ("projectTitle", project.getTitle());
    resultObject->setProperty ("soundName", project.getPluginSoundName());
    resultObject->setProperty ("savedStateSha256", savedHash);
    resultObject->setProperty ("acceptedLiveStateSha256", acceptedHashBeforePlay);
    resultObject->setProperty ("baselineLiveStateSha256", baselineLiveHash);
    resultObject->setProperty ("baselineMatchesAccepted", baselineMatchesAccepted);
    resultObject->setProperty ("prepared", engine.isPrepared());

    engine.setPlaying (true);
    updateStatus();
    const auto startedPlaying = engine.isPlaying();
    juce::Thread::sleep (4500);
    const auto displayBeatAfterWait = engine.getDisplayBeat();
    const auto playheadAdvanced = displayBeatAfterWait > 0.05;
    engine.stopAndRewind();

    resultObject->setProperty ("startedPlaying", startedPlaying);
    resultObject->setProperty ("displayBeatAfterWait", displayBeatAfterWait);
    resultObject->setProperty ("playheadAdvanced", playheadAdvanced);
    resultObject->setProperty ("invalidSampleCount", engine.getInvalidSampleCount());
    resultObject->setProperty ("processorExceptionCount", engine.getProcessorExceptionCount());

    captureSoundCandidate();
    const auto candidateCaptured = soundCandidate.has_value();
    const auto candidateHash = candidateLiveSoundSha256;
    const auto candidateMatchesAccepted = candidateCaptured
                                          && candidateHash.isNotEmpty()
                                          && candidateHash.equalsIgnoreCase (acceptedLiveSoundSha256);
    const auto stateMatchStatus = projectStatusMessage.containsIgnoreCase ("STATE MATCHES A");
    const auto projectCleanAfterCapture = ! project.isDirty();
    const auto noUncapturedStateAfterCapture = ! hasUncapturedLiveSoundState();

    resultObject->setProperty ("candidateCaptured", candidateCaptured);
    resultObject->setProperty ("candidateLiveStateSha256", candidateHash);
    resultObject->setProperty ("candidateMatchesAccepted", candidateMatchesAccepted);
    resultObject->setProperty ("stateMatchStatus", stateMatchStatus);
    resultObject->setProperty ("projectCleanAfterCapture", projectCleanAfterCapture);
    resultObject->setProperty ("noUncapturedStateAfterCapture", noUncapturedStateAfterCapture);
    resultObject->setProperty ("workflowLabelAfterCapture", soundWorkflowLabel.getText());

    rejectSoundCandidate();
    const auto candidateRejected = ! soundCandidate.has_value();
    const auto acceptedRestoredAfterReject = acceptedLiveSoundSha256.isNotEmpty()
                                             && acceptedLiveSoundSha256.equalsIgnoreCase (candidateHash);
    const auto projectCleanAfterReject = ! project.isDirty();
    const auto noUncapturedStateAfterReject = ! hasUncapturedLiveSoundState();
    const auto projectLabelClean = ! projectNameLabel.getText().containsChar ('*');

    auto closeAccepted = std::make_shared<std::atomic<bool>> (false);
    requestClose ([closeAccepted] { closeAccepted->store (true); });
    const auto closeAcceptedWithoutWarning = closeAccepted->load();

    resultObject->setProperty ("candidateRejected", candidateRejected);
    resultObject->setProperty ("acceptedRestoredAfterReject", acceptedRestoredAfterReject);
    resultObject->setProperty ("projectCleanAfterReject", projectCleanAfterReject);
    resultObject->setProperty ("noUncapturedStateAfterReject", noUncapturedStateAfterReject);
    resultObject->setProperty ("projectLabelClean", projectLabelClean);
    resultObject->setProperty ("closeAcceptedWithoutWarning", closeAcceptedWithoutWarning);
    resultObject->setProperty ("finalWorkflowLabel", soundWorkflowLabel.getText());

    const auto passed = opened
                        && project.getPluginSoundName().isNotEmpty()
                        && engine.isPrepared()
                        && baselineMatchesAccepted
                        && startedPlaying
                        && playheadAdvanced
                        && engine.getInvalidSampleCount() == 0
                        && engine.getProcessorExceptionCount() == 0
                        && candidateCaptured
                        && candidateMatchesAccepted
                        && stateMatchStatus
                        && projectCleanAfterCapture
                        && noUncapturedStateAfterCapture
                        && candidateRejected
                        && acceptedRestoredAfterReject
                        && projectCleanAfterReject
                        && noUncapturedStateAfterReject
                        && projectLabelClean
                        && closeAcceptedWithoutWarning;
    resultObject->setProperty ("passed", passed);
    return result;
}

void MainEditorComponent::prepareM5PreviewForSnapshot()
{
    const auto notes = project.getNotes();
    if (notes.empty() || pianoRoll == nullptr)
        return;

    dynamicsScopeCombo.setSelectedId (velocityScopeWholeLoop, juce::dontSendNotification);
    dynamicsStrengthEditor.setText (juce::String (editorVelocityVariationMaximumDelta), false);
    dynamicsSeedEditor.setText (juce::String::formatted (
                                    "%lld",
                                    static_cast<long long> (editorVelocityVariationSeed)),
                                false);
    pianoRoll->setSelectedNote (notes.front().id);
    previewVelocityVariation();
    auditionEditCandidate();
    updateStatus();
}

juce::var MainEditorComponent::runM5WorkflowSelfTest()
{
    constexpr std::int64_t parameterizedVelocityTestSeed = 90210;
    constexpr int parameterizedVelocityTestDelta = 3;

    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);

    clearEditPreview (true);
    clearSoundCandidate();
    project.markClean();
    dynamicsScopeCombo.setSelectedId (velocityScopeWholeLoop, juce::dontSendNotification);
    dynamicsStrengthEditor.setText (juce::String (editorVelocityVariationMaximumDelta), false);
    dynamicsSeedEditor.setText (juce::String::formatted (
                                    "%lld",
                                    static_cast<long long> (editorVelocityVariationSeed)),
                                false);

    const auto notes = project.getNotes();
    if (notes.empty() || pianoRoll == nullptr)
    {
        resultObject->setProperty ("passed", false);
        resultObject->setProperty ("error", "The starter project has no editable note");
        return result;
    }

    const auto original = notes.front();
    const auto beforeHash = project.getContentSha256();
    pianoRoll->setSelectedNote (original.id);

    previewSelectedNoteEdit();
    const auto previewCreated = hasPendingEditPreview();
    const auto firstPreviewStatus = projectStatusMessage;
    const auto* candidate = previewCreated ? editPreview->getCandidateProject() : nullptr;
    const auto candidateNote = candidate != nullptr
                                   ? candidate->findNote (original.id)
                                   : std::nullopt;
    const auto candidateHash = previewCreated ? editPreview->afterContentSha256 : juce::String {};
    const auto activeUnchangedDuringPreview = project.getContentSha256() == beforeHash;
    const auto projectCleanDuringPreview = ! project.isDirty();
    const auto diffCount = previewCreated ? static_cast<int> (editPreview->noteDiffs.size()) : 0;
    const auto updateDiff = previewCreated && diffCount == 1
                            && editPreview->noteDiffs.front().action == NoteEditAction::update;
    const auto soundLaneInterlocked = previewCreated
                                      && ! captureSoundButton.isEnabled()
                                      && ! auditionProjectSoundButton.isEnabled()
                                      && ! auditionCandidateButton.isEnabled()
                                      && ! applySoundButton.isEnabled()
                                      && ! rejectSoundButton.isEnabled();

    const auto savedPreviewFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getNonexistentChildFile ("resonance-m5-save-a",
                                                                ".resonance.json",
                                                                false);
    saveProjectToFile (savedPreviewFile);
    SongProject savedAcceptedProject;
    const auto savedProjectResult = savedAcceptedProject.loadFromFile (savedPreviewFile);
    const auto savePreservedAcceptedA = savedProjectResult.wasOk()
                                        && savedAcceptedProject.getContentSha256() == beforeHash
                                        && project.getContentSha256() == beforeHash
                                        && hasPendingEditPreview();
    savedPreviewFile.deleteFile();
    currentProjectFile = {};

    auditionEditCandidate();
    const auto candidateAuditionSelected = auditioningEditCandidate
                                           && auditionEditCandidateButton.getToggleState()
                                           && ! auditionEditProjectButton.getToggleState();
    const auto activeUnchangedAfterCandidateAudition = project.getContentSha256() == beforeHash;
    auditionEditProject();
    const auto projectAuditionSelected = ! auditioningEditCandidate
                                         && auditionEditProjectButton.getToggleState()
                                         && ! auditionEditCandidateButton.getToggleState();

    rejectEditPreview();
    const auto rejectedWithoutMutation = ! hasPendingEditPreview()
                                         && project.getContentSha256() == beforeHash
                                         && ! project.isDirty();

    previewSelectedNoteEdit();
    const auto applyPreviewCreated = hasPendingEditPreview();
    const auto applyCandidateHash = applyPreviewCreated
                                        ? editPreview->afterContentSha256
                                        : juce::String {};
    auditionEditCandidate();
    applyEditPreview();
    const auto appliedNote = project.findNote (original.id);
    const auto applied = ! hasPendingEditPreview()
                         && appliedNote.has_value()
                         && appliedNote->midiNote == original.midiNote + 1
                         && project.getContentSha256() == applyCandidateHash
                         && project.isDirty();
    const auto applyProducedOneUndo = project.canUndo()
                                      && project.getUndoDescription().containsIgnoreCase ("Apply edit");

    const auto undoPerformed = project.undo();
    const auto undoNote = project.findNote (original.id);
    const auto undoRestoredBefore = undoPerformed
                                    && undoNote.has_value()
                                    && undoNote->midiNote == original.midiNote
                                    && project.getContentSha256() == beforeHash;
    const auto redoPerformed = project.redo();
    const auto redoNote = project.findNote (original.id);
    const auto redoRestoredCandidate = redoPerformed
                                       && redoNote.has_value()
                                       && redoNote->midiNote == original.midiNote + 1
                                       && project.getContentSha256() == applyCandidateHash;

    previewSelectedNoteEdit();
    const auto stalePreviewCreated = hasPendingEditPreview();
    const auto tempoBeforeStaleEdit = project.getTempoBpm();
    project.beginUndoTransaction ("Invalidate pending note proposal");
    project.setTempoBpm (juce::jmin (240.0, tempoBeforeStaleEdit + 1.0));
    const auto stalePreviewInvalidated = stalePreviewCreated
                                         && ! hasPendingEditPreview()
                                         && ! auditioningEditCandidate
                                         && projectStatusMessage.containsIgnoreCase ("STALE");

    const auto staleEditUndone = project.undo();
    const auto appliedEditUndone = project.undo();
    const auto finalNote = project.findNote (original.id);
    const auto finalRestored = staleEditUndone && appliedEditUndone
                               && finalNote.has_value()
                               && finalNote->midiNote == original.midiNote
                               && project.getContentSha256() == beforeHash;
    project.markClean();
    refreshProjectControls();

    previewVelocityVariation();
    const auto seededVelocityPreviewCreated = hasPendingEditPreview()
                                              && editPreview->command.seed.has_value()
                                              && *editPreview->command.seed
                                                     == editorVelocityVariationSeed;
    const auto seededVelocityDiffCount = seededVelocityPreviewCreated
                                             ? static_cast<int> (editPreview->noteDiffs.size())
                                             : 0;
    const auto seededVelocityCandidateSha = seededVelocityPreviewCreated
                                                ? editPreview->afterContentSha256
                                                : juce::String {};
    auto seededVelocityBounded = seededVelocityPreviewCreated
                                 && seededVelocityDiffCount == static_cast<int> (notes.size());
    for (const auto& diff : seededVelocityPreviewCreated
                                ? editPreview->noteDiffs
                                : std::vector<NoteEditDiff> {})
    {
        if (! diff.before.has_value() || ! diff.after.has_value())
        {
            seededVelocityBounded = false;
            break;
        }

        const auto velocityDelta = std::abs (diff.after->velocity - diff.before->velocity);
        seededVelocityBounded = seededVelocityBounded
                                && diff.action == NoteEditAction::update
                                && velocityDelta >= 1
                                && velocityDelta <= editorVelocityVariationMaximumDelta
                                && diff.after->beat == diff.before->beat
                                && diff.after->lengthBeats == diff.before->lengthBeats
                                && diff.after->midiNote == diff.before->midiNote;
    }

    auditionEditCandidate();
    const auto seededVelocityAuditionPreservedA = seededVelocityPreviewCreated
                                                   && auditioningEditCandidate
                                                   && project.getContentSha256() == beforeHash
                                                   && ! project.isDirty();
    rejectEditPreview();
    const auto seededVelocityRejectedWithoutMutation = ! hasPendingEditPreview()
                                                        && project.getContentSha256() == beforeHash
                                                        && ! project.isDirty();

    previewVelocityVariation();
    const auto seededVelocityRepeatMatched = hasPendingEditPreview()
                                               && editPreview->afterContentSha256
                                                      == seededVelocityCandidateSha;
    applyEditPreview();
    const auto seededVelocityApplied = ! hasPendingEditPreview()
                                       && project.getContentSha256()
                                              == seededVelocityCandidateSha
                                       && project.isDirty();
    const auto seededVelocityApplyProducedOneUndo = project.canUndo()
                                                    && project.getUndoDescription()
                                                           .containsIgnoreCase ("Apply edit");
    const auto seededVelocityUndoPerformed = project.undo();
    const auto seededVelocityUndoRestoredA = seededVelocityUndoPerformed
                                             && project.getContentSha256() == beforeHash;
    project.markClean();
    refreshProjectControls();

    dynamicsScopeCombo.setSelectedId (velocityScopeSelectedNote,
                                      juce::dontSendNotification);
    dynamicsStrengthEditor.setText (juce::String (parameterizedVelocityTestDelta), false);
    dynamicsSeedEditor.setText (juce::String::formatted (
                                    "%lld",
                                    static_cast<long long> (parameterizedVelocityTestSeed)),
                                false);
    refreshEditPreviewControls();

    SeededVelocityVariation parameterizedRequest;
    const auto parameterizedSettingsResult = readVelocityVariationControls (parameterizedRequest);
    const auto parameterControlsAvailable = parameterizedSettingsResult.wasOk()
                                            && parameterizedRequest.noteIds.size() == 1
                                            && parameterizedRequest.noteIds.front() == original.id
                                            && parameterizedRequest.maximumDelta
                                                   == parameterizedVelocityTestDelta
                                            && parameterizedRequest.seed
                                                   == parameterizedVelocityTestSeed
                                            && dynamicsScopeCombo.isEnabled()
                                            && dynamicsStrengthEditor.isEnabled()
                                            && dynamicsSeedEditor.isEnabled()
                                            && previewDynamicsButton.isEnabled();

    previewVelocityVariation();
    const auto parameterizedPreviewCreated = hasPendingEditPreview()
                                             && editPreview->command.seed.has_value()
                                             && *editPreview->command.seed
                                                    == parameterizedVelocityTestSeed;
    const auto parameterizedDiffCount = parameterizedPreviewCreated
                                            ? static_cast<int> (editPreview->noteDiffs.size())
                                            : 0;
    const auto parameterizedCandidateSha = parameterizedPreviewCreated
                                               ? editPreview->afterContentSha256
                                               : juce::String {};
    const auto parameterizedSummaryMatched = parameterizedPreviewCreated
                                             && editPreview->command.summary.contains (
                                                 "up to "
                                                 + juce::String (parameterizedVelocityTestDelta));
    auto parameterizedTargetMatched = parameterizedPreviewCreated
                                      && parameterizedDiffCount == 1
                                      && editPreview->noteDiffs.front().noteId == original.id;
    auto parameterizedBounded = parameterizedTargetMatched;
    if (parameterizedTargetMatched)
    {
        const auto& diff = editPreview->noteDiffs.front();
        if (! diff.before.has_value() || ! diff.after.has_value())
        {
            parameterizedBounded = false;
        }
        else
        {
            const auto velocityDelta = std::abs (diff.after->velocity - diff.before->velocity);
            parameterizedBounded = diff.action == NoteEditAction::update
                                   && velocityDelta >= 1
                                   && velocityDelta <= parameterizedVelocityTestDelta
                                   && diff.after->beat == diff.before->beat
                                   && diff.after->lengthBeats == diff.before->lengthBeats
                                   && diff.after->midiNote == diff.before->midiNote;
        }
    }
    const auto parameterizedCandidateDiffered = parameterizedCandidateSha.isNotEmpty()
                                                && parameterizedCandidateSha
                                                       != seededVelocityCandidateSha;

    auditionEditCandidate();
    const auto parameterizedAuditionPreservedA = parameterizedPreviewCreated
                                                  && auditioningEditCandidate
                                                  && project.getContentSha256() == beforeHash
                                                  && ! project.isDirty();
    rejectEditPreview();
    const auto parameterizedRejectedWithoutMutation = ! hasPendingEditPreview()
                                                       && project.getContentSha256() == beforeHash
                                                       && ! project.isDirty();

    previewVelocityVariation();
    const auto parameterizedRepeatMatched = hasPendingEditPreview()
                                              && editPreview->afterContentSha256
                                                     == parameterizedCandidateSha;
    applyEditPreview();
    const auto parameterizedApplied = ! hasPendingEditPreview()
                                      && project.getContentSha256()
                                             == parameterizedCandidateSha
                                      && project.isDirty();
    const auto parameterizedApplyProducedOneUndo = project.canUndo()
                                                   && project.getUndoDescription()
                                                          .containsIgnoreCase ("Apply edit");
    const auto parameterizedUndoPerformed = project.undo();
    const auto parameterizedUndoRestoredA = parameterizedUndoPerformed
                                            && project.getContentSha256() == beforeHash;
    project.markClean();

    dynamicsStrengthEditor.setText ("33", false);
    refreshEditPreviewControls();
    SeededVelocityVariation invalidRequest;
    const auto invalidDynamicsSettingsBlocked = readVelocityVariationControls (invalidRequest).failed()
                                                && ! previewDynamicsButton.isEnabled();

    dynamicsScopeCombo.setSelectedId (velocityScopeWholeLoop, juce::dontSendNotification);
    dynamicsStrengthEditor.setText (juce::String (editorVelocityVariationMaximumDelta), false);
    dynamicsSeedEditor.setText (juce::String::formatted (
                                    "%lld",
                                    static_cast<long long> (editorVelocityVariationSeed)),
                                false);
    refreshProjectControls();

    auto closeAccepted = std::make_shared<std::atomic<bool>> (false);
    requestClose ([closeAccepted] { closeAccepted->store (true); });
    const auto closeAcceptedWithoutWarning = closeAccepted->load();

    resultObject->setProperty ("selectedNoteId", original.id);
    resultObject->setProperty ("originalPitch", original.midiNote);
    resultObject->setProperty ("candidatePitch",
                               candidateNote.has_value() ? candidateNote->midiNote : -1);
    resultObject->setProperty ("beforeContentSha256", beforeHash);
    resultObject->setProperty ("candidateContentSha256", candidateHash);
    resultObject->setProperty ("previewCreated", previewCreated);
    resultObject->setProperty ("firstPreviewStatus", firstPreviewStatus);
    resultObject->setProperty ("activeUnchangedDuringPreview", activeUnchangedDuringPreview);
    resultObject->setProperty ("projectCleanDuringPreview", projectCleanDuringPreview);
    resultObject->setProperty ("diffCount", diffCount);
    resultObject->setProperty ("updateDiff", updateDiff);
    resultObject->setProperty ("soundLaneInterlocked", soundLaneInterlocked);
    resultObject->setProperty ("savePreservedAcceptedA", savePreservedAcceptedA);
    resultObject->setProperty ("candidateAuditionSelected", candidateAuditionSelected);
    resultObject->setProperty ("activeUnchangedAfterCandidateAudition",
                               activeUnchangedAfterCandidateAudition);
    resultObject->setProperty ("projectAuditionSelected", projectAuditionSelected);
    resultObject->setProperty ("rejectedWithoutMutation", rejectedWithoutMutation);
    resultObject->setProperty ("applyPreviewCreated", applyPreviewCreated);
    resultObject->setProperty ("applied", applied);
    resultObject->setProperty ("applyProducedOneUndo", applyProducedOneUndo);
    resultObject->setProperty ("undoRestoredBefore", undoRestoredBefore);
    resultObject->setProperty ("redoRestoredCandidate", redoRestoredCandidate);
    resultObject->setProperty ("stalePreviewInvalidated", stalePreviewInvalidated);
    resultObject->setProperty ("finalRestored", finalRestored);
    resultObject->setProperty ("seededVelocitySeed", editorVelocityVariationSeed);
    resultObject->setProperty ("seededVelocityMaximumDelta",
                               editorVelocityVariationMaximumDelta);
    resultObject->setProperty ("seededVelocityPreviewCreated",
                               seededVelocityPreviewCreated);
    resultObject->setProperty ("seededVelocityDiffCount", seededVelocityDiffCount);
    resultObject->setProperty ("seededVelocityCandidateSha256",
                               seededVelocityCandidateSha);
    resultObject->setProperty ("seededVelocityBounded", seededVelocityBounded);
    resultObject->setProperty ("seededVelocityAuditionPreservedA",
                               seededVelocityAuditionPreservedA);
    resultObject->setProperty ("seededVelocityRejectedWithoutMutation",
                               seededVelocityRejectedWithoutMutation);
    resultObject->setProperty ("seededVelocityRepeatMatched",
                               seededVelocityRepeatMatched);
    resultObject->setProperty ("seededVelocityApplied", seededVelocityApplied);
    resultObject->setProperty ("seededVelocityApplyProducedOneUndo",
                               seededVelocityApplyProducedOneUndo);
    resultObject->setProperty ("seededVelocityUndoRestoredA",
                               seededVelocityUndoRestoredA);
    resultObject->setProperty ("parameterControlsAvailable", parameterControlsAvailable);
    resultObject->setProperty ("parameterizedScope", "selectedNote");
    resultObject->setProperty ("parameterizedSeed", parameterizedVelocityTestSeed);
    resultObject->setProperty ("parameterizedMaximumDelta",
                               parameterizedVelocityTestDelta);
    resultObject->setProperty ("parameterizedPreviewCreated",
                               parameterizedPreviewCreated);
    resultObject->setProperty ("parameterizedDiffCount", parameterizedDiffCount);
    resultObject->setProperty ("parameterizedCandidateSha256",
                               parameterizedCandidateSha);
    resultObject->setProperty ("parameterizedSummaryMatched",
                               parameterizedSummaryMatched);
    resultObject->setProperty ("parameterizedTargetMatched",
                               parameterizedTargetMatched);
    resultObject->setProperty ("parameterizedBounded", parameterizedBounded);
    resultObject->setProperty ("parameterizedCandidateDiffered",
                               parameterizedCandidateDiffered);
    resultObject->setProperty ("parameterizedAuditionPreservedA",
                               parameterizedAuditionPreservedA);
    resultObject->setProperty ("parameterizedRejectedWithoutMutation",
                               parameterizedRejectedWithoutMutation);
    resultObject->setProperty ("parameterizedRepeatMatched",
                               parameterizedRepeatMatched);
    resultObject->setProperty ("parameterizedApplied", parameterizedApplied);
    resultObject->setProperty ("parameterizedApplyProducedOneUndo",
                               parameterizedApplyProducedOneUndo);
    resultObject->setProperty ("parameterizedUndoRestoredA",
                               parameterizedUndoRestoredA);
    resultObject->setProperty ("invalidDynamicsSettingsBlocked",
                               invalidDynamicsSettingsBlocked);
    resultObject->setProperty ("closeAcceptedWithoutWarning", closeAcceptedWithoutWarning);
    resultObject->setProperty ("invalidSampleCount", engine.getInvalidSampleCount());
    resultObject->setProperty ("processorExceptionCount", engine.getProcessorExceptionCount());

    const auto passed = previewCreated
                        && candidateNote.has_value()
                        && candidateNote->midiNote == original.midiNote + 1
                        && activeUnchangedDuringPreview
                        && projectCleanDuringPreview
                        && updateDiff
                        && soundLaneInterlocked
                        && savePreservedAcceptedA
                        && candidateAuditionSelected
                        && activeUnchangedAfterCandidateAudition
                        && projectAuditionSelected
                        && rejectedWithoutMutation
                        && applyPreviewCreated
                        && applied
                        && applyProducedOneUndo
                        && undoRestoredBefore
                        && redoRestoredCandidate
                        && stalePreviewInvalidated
                        && finalRestored
                        && seededVelocityPreviewCreated
                        && seededVelocityBounded
                        && seededVelocityAuditionPreservedA
                        && seededVelocityRejectedWithoutMutation
                        && seededVelocityRepeatMatched
                        && seededVelocityApplied
                        && seededVelocityApplyProducedOneUndo
                        && seededVelocityUndoRestoredA
                        && parameterControlsAvailable
                        && parameterizedPreviewCreated
                        && parameterizedSummaryMatched
                        && parameterizedTargetMatched
                        && parameterizedBounded
                        && parameterizedCandidateDiffered
                        && parameterizedAuditionPreservedA
                        && parameterizedRejectedWithoutMutation
                        && parameterizedRepeatMatched
                        && parameterizedApplied
                        && parameterizedApplyProducedOneUndo
                        && parameterizedUndoRestoredA
                        && invalidDynamicsSettingsBlocked
                        && closeAcceptedWithoutWarning
                        && engine.getInvalidSampleCount() == 0
                        && engine.getProcessorExceptionCount() == 0;
    resultObject->setProperty ("passed", passed);
    return result;
}

juce::var MainEditorComponent::runM6AuthoringSelfTest (const juce::File& projectFile)
{
    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);
    resultObject->setProperty ("editorVersion", JUCE_APPLICATION_VERSION_STRING);
    resultObject->setProperty ("projectPath", projectFile.getFileName());
    resultObject->setProperty ("audioEmitted", false);

    const auto initialTrackCount = project.getTrackCount();
    const auto preloadedPluginCount = static_cast<int> (engine.getActivePluginCount());
    const auto distinctRuntimeInstances = engine.getPluginForTrack (0) != nullptr
                                          && engine.getPluginForTrack (1) != nullptr
                                          && engine.getPluginForTrack (0)
                                                 != engine.getPluginForTrack (1);
    const auto firstTrackId = project.getTrackId (0);
    const auto firstClipId = project.getClipId (0);

    addInstrumentTrack();
    const auto addTrackSucceeded = project.getTrackCount() == 2
                                   && project.getActiveTrackIndex() == 1;
    const auto secondTrackId = addTrackSucceeded ? project.getTrackId (1) : juce::String {};
    const auto secondClipId = addTrackSucceeded ? project.getClipId (1) : juce::String {};
    const auto stableDistinctIds = addTrackSucceeded
                                   && firstTrackId.isNotEmpty()
                                   && secondTrackId.isNotEmpty()
                                   && firstTrackId != secondTrackId
                                   && firstClipId != secondClipId;

    juce::MemoryBlock firstProjectState;
    juce::MemoryBlock secondProjectState;
    const auto duplicatedStateExact = addTrackSucceeded
                                      && project.getPluginStateForTrack (0,
                                                                         firstProjectState).wasOk()
                                      && project.getPluginStateForTrack (1,
                                                                         secondProjectState).wasOk()
                                      && firstProjectState == secondProjectState;

    const auto runtimeStatesMatchProject = [this]
    {
        for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
        {
            juce::MemoryBlock projectState;
            juce::MemoryBlock runtimeState;
            if (project.getPluginStateForTrack (trackIndex, projectState).failed()
                || engine.capturePluginStateForTrack (
                       static_cast<std::size_t> (trackIndex), runtimeState).failed()
                || projectState != runtimeState)
                return false;
        }
        return true;
    };

    const auto runtimeStateAlignedAfterAdd = addTrackSucceeded
                                             && runtimeStatesMatchProject();

    auto independentNotes = false;
    if (addTrackSucceeded)
    {
        const auto firstBefore = project.createSequenceSnapshotForTrack (0);
        auto secondNotes = project.getNotes();
        if (! secondNotes.empty())
        {
            project.beginUndoTransaction ("M6 independent note edit");
            secondNotes.front().velocity = secondNotes.front().velocity < 127
                                               ? secondNotes.front().velocity + 1
                                               : secondNotes.front().velocity - 1;
            const auto changed = project.updateNote (secondNotes.front());
            const auto firstAfter = project.createSequenceSnapshotForTrack (0);
            const auto secondAfter = project.createSequenceSnapshotForTrack (1);
            independentNotes = changed
                               && firstAfter.noteCount == firstBefore.noteCount
                               && firstAfter.noteCount > 0
                               && firstAfter.notes[0].velocity == firstBefore.notes[0].velocity
                               && secondAfter.notes[0].velocity != firstAfter.notes[0].velocity;
        }
    }

    const TrackMixerSettings firstMix { -6.0, -0.65, false, false };
    const TrackMixerSettings secondMix { -9.0, 0.65, true, false };
    project.beginUndoTransaction ("M6 independent mixer settings");
    const auto firstMixResult = project.setTrackMixerSettingsForTrack (0, firstMix);
    const auto secondMixResult = project.setTrackMixerSettingsForTrack (1, secondMix);
    const auto storedFirstMix = project.getTrackMixerSettings (0);
    const auto storedSecondMix = project.getTrackMixerSettings (1);
    const auto independentMixerSettings = firstMixResult.wasOk() && secondMixResult.wasOk()
                                          && storedFirstMix.gainDecibels == -6.0
                                          && storedFirstMix.pan == -0.65
                                          && ! storedFirstMix.muted
                                          && storedSecondMix.gainDecibels == -9.0
                                          && storedSecondMix.pan == 0.65
                                          && storedSecondMix.muted;

    moveActiveTrack (-1);
    const auto reorderSucceeded = project.getTrackId (0) == secondTrackId
                                  && project.getTrackId (1) == firstTrackId
                                  && project.getActiveTrackIndex() == 0;
    const auto runtimeStateAlignedAfterReorder = reorderSucceeded
                                                 && runtimeStatesMatchProject();

    const auto undoReorderPerformed = project.undo();
    const auto undoReorderRestored = undoReorderPerformed
                                     && project.getTrackId (0) == firstTrackId
                                     && project.getTrackId (1) == secondTrackId
                                     && project.getActiveTrackIndex() == 1
                                     && runtimeStatesMatchProject();

    saveProjectToFile (projectFile);
    const auto saveSucceeded = currentProjectFile == projectFile
                               && projectFile.existsAsFile()
                               && ! project.isDirty();

    SongProject reopened;
    const auto reopenResult = reopened.loadFromFile (projectFile);
    const auto reopenedSchemaVersion = reopenResult.wasOk() ? reopened.getSchemaVersion() : -1;
    const auto reopenedTrackCount = reopenResult.wasOk() ? reopened.getTrackCount() : 0;
    const auto reopenedOrderPreserved = reopenResult.wasOk()
                                        && reopenedTrackCount == 2
                                        && reopened.getTrackId (0) == firstTrackId
                                        && reopened.getTrackId (1) == secondTrackId;
    const auto reopenedFirstMix = reopenResult.wasOk()
                                      ? reopened.getTrackMixerSettings (0)
                                      : TrackMixerSettings {};
    const auto reopenedSecondMix = reopenResult.wasOk()
                                       ? reopened.getTrackMixerSettings (1)
                                       : TrackMixerSettings {};
    const auto reopenedMixerPreserved = reopenResult.wasOk()
                                        && reopenedFirstMix.gainDecibels == -6.0
                                        && reopenedFirstMix.pan == -0.65
                                        && reopenedSecondMix.gainDecibels == -9.0
                                        && reopenedSecondMix.pan == 0.65
                                        && reopenedSecondMix.muted;
    const auto reopenedIndependentNotes = reopenResult.wasOk()
                                          && reopened.createSequenceSnapshotForTrack (0).notes[0].velocity
                                                 != reopened.createSequenceSnapshotForTrack (1).notes[0].velocity;

    removeActiveTrack();
    const auto removeSucceeded = project.getTrackCount() == 1;
    const auto undoRemoveRestored = project.undo()
                                    && project.getTrackCount() == 2
                                    && project.getTrackId (0) == firstTrackId
                                    && project.getTrackId (1) == secondTrackId
                                    && runtimeStatesMatchProject();

    const auto noRuntimeFault = runtimeProjectSyncError.isEmpty()
                                && engine.getInvalidSampleCount() == 0
                                && engine.getProcessorExceptionCount() == 0;

    resultObject->setProperty ("initialTrackCount", initialTrackCount);
    resultObject->setProperty ("preloadedPluginCount", preloadedPluginCount);
    resultObject->setProperty ("distinctRuntimeInstances", distinctRuntimeInstances);
    resultObject->setProperty ("addTrackSucceeded", addTrackSucceeded);
    resultObject->setProperty ("activeTrackAfterAdd", addTrackSucceeded ? 1 : -1);
    resultObject->setProperty ("firstTrackId", firstTrackId);
    resultObject->setProperty ("secondTrackId", secondTrackId);
    resultObject->setProperty ("stableDistinctIds", stableDistinctIds);
    resultObject->setProperty ("duplicatedStateExact", duplicatedStateExact);
    resultObject->setProperty ("runtimeStateAlignedAfterAdd", runtimeStateAlignedAfterAdd);
    resultObject->setProperty ("independentNotes", independentNotes);
    resultObject->setProperty ("independentMixerSettings", independentMixerSettings);
    resultObject->setProperty ("reorderSucceeded", reorderSucceeded);
    resultObject->setProperty ("runtimeStateAlignedAfterReorder",
                               runtimeStateAlignedAfterReorder);
    resultObject->setProperty ("undoReorderRestored", undoReorderRestored);
    resultObject->setProperty ("saveSucceeded", saveSucceeded);
    resultObject->setProperty ("reopenedSchemaVersion", reopenedSchemaVersion);
    resultObject->setProperty ("reopenedTrackCount", reopenedTrackCount);
    resultObject->setProperty ("reopenedOrderPreserved", reopenedOrderPreserved);
    resultObject->setProperty ("reopenedMixerPreserved", reopenedMixerPreserved);
    resultObject->setProperty ("reopenedIndependentNotes", reopenedIndependentNotes);
    resultObject->setProperty ("removeSucceeded", removeSucceeded);
    resultObject->setProperty ("undoRemoveRestored", undoRemoveRestored);
    resultObject->setProperty ("invalidSampleCount", engine.getInvalidSampleCount());
    resultObject->setProperty ("processorExceptionCount",
                               engine.getProcessorExceptionCount());

    const auto passed = initialTrackCount == 1
                        && preloadedPluginCount == SongProject::maxProjectTracks
                        && distinctRuntimeInstances
                        && addTrackSucceeded
                        && stableDistinctIds
                        && duplicatedStateExact
                        && runtimeStateAlignedAfterAdd
                        && independentNotes
                        && independentMixerSettings
                        && reorderSucceeded
                        && runtimeStateAlignedAfterReorder
                        && undoReorderRestored
                        && saveSucceeded
                        && reopenedSchemaVersion == SongProject::currentSchemaVersion
                        && reopenedTrackCount == 2
                        && reopenedOrderPreserved
                        && reopenedMixerPreserved
                        && reopenedIndependentNotes
                        && removeSucceeded
                        && undoRemoveRestored
                        && noRuntimeFault;
    resultObject->setProperty ("passed", passed);
    project.markClean();
    return result;
}

void MainEditorComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                            title,
                                            message,
                                            "OK",
                                            this);
}

bool MainEditorComponent::keyPressed (const juce::KeyPress& key)
{
    const auto command = key.getModifiers().isCommandDown();
    if (command && key.getKeyCode() == 'S') { saveProject(); return true; }
    if (command && key.getKeyCode() == 'O') { confirmDiscardIfNeeded ([this] { chooseProjectToOpen(); }); return true; }
    if (command && key.getKeyCode() == 'N') { confirmDiscardIfNeeded ([this] { startNewProject(); }); return true; }
    if (command && key.getKeyCode() == 'Z')
    {
        performUndoRedo (key.getModifiers().isShiftDown());
        return true;
    }
    if (command && key.getKeyCode() == 'Y') { performUndoRedo (true); return true; }
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        engine.setPlaying (! engine.isPlaying());
        return true;
    }
    return false;
}

void MainEditorComponent::saveSettings()
{
    if (settingsFile == nullptr)
        return;

    if (auto xml = deviceManager.createStateXml())
        settingsFile->setValue ("audioDeviceState", xml->toString());
    settingsFile->setValue ("masterGainDb", gainSlider.getValue());
    settingsFile->saveIfNeeded();
}

void MainEditorComponent::timerCallback()
{
    engine.flushPendingSequence();
    if (pianoRoll != nullptr)
        pianoRoll->setPlayheadBeat (engine.getDisplayBeat());

    const auto beat = engine.getDisplayBeat();
    const auto bars = juce::jmax (1, juce::roundToInt (project.getLoopLengthBeats() / 4.0));
    const auto bar = juce::jlimit (1, bars, static_cast<int> (beat / 4.0) + 1);
    const auto beatInBar = std::fmod (beat, 4.0) + 1.0;
    transportPositionLabel.setText ("BAR " + juce::String (bar) + "  /  BEAT "
                                    + juce::String (beatInBar, 2),
                                    juce::dontSendNotification);

    playButton.setButtonText (engine.isPlaying() ? "Pause" : "Play loop");
    const auto activeTrack = project.getActiveTrackIndex();
    const auto leftPeak = activeTrack >= 0
                              ? engine.getTrackLeftPeak (static_cast<std::size_t> (activeTrack))
                              : 0.0f;
    const auto rightPeak = activeTrack >= 0
                               ? engine.getTrackRightPeak (static_cast<std::size_t> (activeTrack))
                               : 0.0f;
    displayedLeftPeak = juce::jmax (leftPeak, displayedLeftPeak * 0.88f);
    displayedRightPeak = juce::jmax (rightPeak, displayedRightPeak * 0.88f);
    deviceSummaryLabel.setText (formatDeviceSummary (deviceManager), juce::dontSendNotification);
    updateStatus();
    repaint (trackCardBounds);
}

void MainEditorComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    deviceStartupError.clear();
    updateStatus();
}

void MainEditorComponent::updateStatus()
{
    const auto* device = deviceManager.getCurrentAudioDevice();
    const auto clips = engine.getClippedSampleCount();
    const auto invalid = engine.getInvalidSampleCount();
    const auto exceptions = engine.getProcessorExceptionCount();
    const auto oversized = engine.getOversizedBlockCount();
    const auto xruns = device != nullptr ? device->getXRunCount() : -1;
    const auto cpu = deviceManager.getCpuUsage() * 100.0;

    juce::String status;
    juce::Colour statusColour;
    if (getActivePlugin() == nullptr)
    {
        status = "PLUGIN OFFLINE";
        statusColour = danger;
    }
    else if (device == nullptr || ! engine.isPrepared())
    {
        status = "CHOOSE AUDIO OUTPUT";
        statusColour = warning;
    }
    else if (runtimeProjectSyncError.isNotEmpty())
    {
        status = "TRACK STATE ERROR / STOPPED SAFE";
        statusColour = danger;
    }
    else if (exceptions > 0 || invalid > 0 || oversized > 0)
    {
        status = "AUDIO FAULT / STOPPED SAFE";
        statusColour = danger;
    }
    else if (clips > lastClipCount)
    {
        status = "OUTPUT SAFETY ACTIVE";
        statusColour = warning;
    }
    else
    {
        status = engine.isPlaying() ? "PLAYING / WASAPI ONLINE" : "READY / WASAPI ONLINE";
        statusColour = primary;
    }

    lastClipCount = clips;
    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::backgroundColourId, statusColour.withAlpha (0.15f));
    statusLabel.setColour (juce::Label::outlineColourId, statusColour.withAlpha (0.65f));
    statusLabel.setColour (juce::Label::textColourId, statusColour);

    juce::String diagnostic = "CPU " + juce::String (cpu, 1) + "%  /  XRUN "
                              + (xruns >= 0 ? juce::String (xruns) : "N/A")
                              + "  /  CLIPS " + juce::String (clips)
                              + "  /  INVALID " + juce::String (invalid);
    if (startupError.isNotEmpty())
        diagnostic = startupError;
    else if (deviceStartupError.isNotEmpty())
        diagnostic = deviceStartupError;
    else if (engine.getLastDeviceError().isNotEmpty())
        diagnostic = engine.getLastDeviceError();
    else if (runtimeProjectSyncError.isNotEmpty())
        diagnostic = runtimeProjectSyncError;
    else if (projectStatusMessage.isNotEmpty())
        diagnostic = projectStatusMessage;

    diagnosticLabel.setText (diagnostic, juce::dontSendNotification);
    diagnosticLabel.setColour (juce::Label::textColourId,
                               (startupError.isNotEmpty() || runtimeProjectSyncError.isNotEmpty()
                                || invalid > 0 || exceptions > 0)
                                   ? danger
                                   : textMuted);

    const auto ready = getActivePlugin() != nullptr && device != nullptr && engine.isPrepared();
    playButton.setEnabled (ready);
    stopButton.setEnabled (getActivePlugin() != nullptr);
    panicButton.setEnabled (getActivePlugin() != nullptr);
    saveButton.setEnabled (getActivePlugin() != nullptr);
    pluginEditorButton.setEnabled (getActivePlugin() != nullptr && pluginRecord.hasEditor);
}

void MainEditorComponent::drawCard (juce::Graphics& graphics, juce::Rectangle<int> rectangle) const
{
    const auto bounds = rectangle.toFloat();
    graphics.setColour (juce::Colours::black.withAlpha (0.23f));
    graphics.fillRoundedRectangle (bounds.translated (0.0f, 3.0f), 13.0f);
    graphics.setColour (card);
    graphics.fillRoundedRectangle (bounds, 13.0f);
    graphics.setColour (cardEdge.withAlpha (0.8f));
    graphics.drawRoundedRectangle (bounds.reduced (0.5f), 13.0f, 1.0f);
}

void MainEditorComponent::paint (juce::Graphics& graphics)
{
    juce::ColourGradient gradient (juce::Colour::fromRGB (16, 25, 39), 0.0f, 0.0f,
                                   background, 0.0f, static_cast<float> (getHeight()), false);
    gradient.addColour (0.45, background);
    graphics.setGradientFill (gradient);
    graphics.fillAll();
    graphics.setColour (primary.withAlpha (0.10f));
    graphics.fillEllipse (-160.0f, -220.0f, 520.0f, 520.0f);
    graphics.setColour (secondary.withAlpha (0.06f));
    graphics.fillEllipse (static_cast<float> (getWidth() - 280), -150.0f, 430.0f, 430.0f);

    drawCard (graphics, transportCardBounds);
    drawCard (graphics, trackCardBounds);
    drawCard (graphics, loopCardBounds);
    drawCard (graphics, keyboardCardBounds);
    drawCard (graphics, deviceCardBounds);

    graphics.setFont (uiFont (11.0f, juce::Font::bold));
    graphics.setColour (textMuted);
    graphics.drawText ("CLICK TO ADD  /  DRAG TO MOVE  /  DRAG RIGHT EDGE TO RESIZE  /  DELETE TO REMOVE",
                       loopCardBounds.reduced (16).removeFromTop (20),
                       juce::Justification::centredLeft);
    graphics.drawText ("MANUAL AUDITION  /  SHARED WITH THE SURGE WINDOW",
                       keyboardCardBounds.reduced (16).removeFromTop (22),
                       juce::Justification::centredLeft);
    graphics.drawText ("AUDIO + MIDI DEVICE",
                       deviceCardBounds.reduced (16).removeFromTop (22),
                       juce::Justification::centredLeft);

    if (! editProposalBounds.isEmpty())
    {
        const auto proposal = editProposalBounds.toFloat();
        graphics.setColour (background.withAlpha (0.58f));
        graphics.fillRoundedRectangle (proposal, 9.0f);
        graphics.setColour (secondary.withAlpha (0.42f));
        graphics.drawRoundedRectangle (proposal.reduced (0.5f), 9.0f, 1.0f);
        graphics.setFont (uiFont (10.5f, juce::Font::bold));
        graphics.setColour (secondary);
        graphics.drawText ("M5 NOTE PROPOSAL  /  A-B PREVIEW",
                           editProposalBounds.reduced (10).removeFromTop (20),
                           juce::Justification::centredLeft);
    }

    auto meterArea = trackCardBounds.reduced (16).removeFromRight (30).toFloat();
    meterArea.removeFromTop (6.0f);
    meterArea.removeFromBottom (6.0f);
    const auto drawMeter = [&graphics, meterArea] (float level, float x)
    {
        auto well = juce::Rectangle<float> (x, meterArea.getY(), 8.0f, meterArea.getHeight());
        graphics.setColour (background.withAlpha (0.9f));
        graphics.fillRoundedRectangle (well, 3.0f);
        const auto normalised = juce::jlimit (0.0f, 1.0f, std::sqrt (level));
        auto fill = well.withTop (well.getBottom() - well.getHeight() * normalised);
        graphics.setColour (normalised > 0.9f ? danger : primary);
        graphics.fillRoundedRectangle (fill, 3.0f);
    };
    drawMeter (displayedLeftPeak, meterArea.getX());
    drawMeter (displayedRightPeak, meterArea.getX() + 12.0f);
}

void MainEditorComponent::resized()
{
    auto area = getLocalBounds().reduced (24);
    headerBounds = area.removeFromTop (58);
    area.removeFromTop (8);
    transportCardBounds = area.removeFromTop (70);
    area.removeFromTop (12);

    auto body = area;
    deviceCardBounds = body.removeFromRight (326);
    body.removeFromRight (12);
    trackCardBounds = body.removeFromTop (221);
    body.removeFromTop (12);
    keyboardCardBounds = body.removeFromBottom (154);
    body.removeFromBottom (12);
    loopCardBounds = body;

    auto header = headerBounds;
    statusLabel.setBounds (header.removeFromRight (222).reduced (0, 10));
    header.removeFromRight (8);
    for (auto* button : { &redoButton, &undoButton, &saveButton, &openButton, &newButton })
    {
        button->setBounds (header.removeFromRight (64).reduced (2, 11));
        header.removeFromRight (2);
    }
    titleLabel.setBounds (header.removeFromLeft (188));
    auto projectHeader = header.reduced (4, 2);
    subtitleLabel.setBounds (projectHeader.removeFromTop (24));
    projectNameLabel.setBounds (projectHeader);

    auto transport = transportCardBounds.reduced (14);
    playButton.setBounds (transport.removeFromLeft (104));
    transport.removeFromLeft (7);
    stopButton.setBounds (transport.removeFromLeft (62));
    transport.removeFromLeft (7);
    panicButton.setBounds (transport.removeFromLeft (66));
    transport.removeFromLeft (12);
    transportPositionLabel.setBounds (transport.removeFromLeft (122));
    transport.removeFromLeft (5);
    bpmLabel.setBounds (transport.removeFromLeft (36));
    bpmSlider.setBounds (transport.removeFromLeft (150));
    transport.removeFromLeft (5);
    gainLabel.setBounds (transport.removeFromLeft (56));
    gainSlider.setBounds (transport);

    auto track = trackCardBounds.reduced (16);
    track.removeFromRight (44);
    auto trackHeader = track.removeFromTop (43);
    pluginEditorButton.setBounds (trackHeader.removeFromRight (138).reduced (0, 4));
    trackHeader.removeFromRight (12);
    trackNameLabel.setBounds (trackHeader.removeFromTop (24));
    trackMetaLabel.setBounds (trackHeader);
    track.removeFromTop (3);
    auto mixerControls = track.removeFromTop (32);
    trackSelector.setBounds (mixerControls.removeFromLeft (140).reduced (0, 2));
    mixerControls.removeFromLeft (5);
    addTrackButton.setBounds (mixerControls.removeFromLeft (62));
    mixerControls.removeFromLeft (5);
    removeTrackButton.setBounds (mixerControls.removeFromLeft (62));
    mixerControls.removeFromLeft (5);
    moveTrackLeftButton.setBounds (mixerControls.removeFromLeft (32));
    mixerControls.removeFromLeft (4);
    moveTrackRightButton.setBounds (mixerControls.removeFromLeft (32));
    mixerControls.removeFromLeft (10);
    trackGainLabel.setBounds (mixerControls.removeFromLeft (34));
    trackGainSlider.setBounds (mixerControls.removeFromLeft (120));
    mixerControls.removeFromLeft (6);
    trackPanLabel.setBounds (mixerControls.removeFromLeft (30));
    trackPanSlider.setBounds (mixerControls.removeFromLeft (90));
    mixerControls.removeFromLeft (6);
    trackMuteButton.setBounds (mixerControls.removeFromLeft (60));
    mixerControls.removeFromLeft (5);
    trackSoloButton.setBounds (mixerControls.removeFromLeft (56));
    track.removeFromTop (3);
    soundWorkflowLabel.setBounds (track.removeFromTop (20));
    track.removeFromTop (3);
    auto soundControls = track.removeFromTop (30);
    soundNameEditor.setBounds (soundControls.removeFromLeft (180));
    soundControls.removeFromLeft (7);
    captureSoundButton.setBounds (soundControls.removeFromLeft (88));
    soundControls.removeFromLeft (7);
    auditionProjectSoundButton.setBounds (soundControls.removeFromLeft (84));
    soundControls.removeFromLeft (7);
    auditionCandidateButton.setBounds (soundControls.removeFromLeft (84));
    soundControls.removeFromLeft (7);
    applySoundButton.setBounds (soundControls.removeFromLeft (72));
    soundControls.removeFromLeft (7);
    rejectSoundButton.setBounds (soundControls.removeFromLeft (76));

    track.removeFromTop (5);
    auto shelfControls = track.removeFromTop (28);
    shelfLabel.setBounds (shelfControls.removeFromLeft (46));
    shelfCombo.setBounds (shelfControls.removeFromLeft (180).reduced (0, 1));
    shelfControls.removeFromLeft (7);
    loadShelfButton.setBounds (shelfControls.removeFromLeft (88));
    shelfControls.removeFromLeft (7);
    saveShelfButton.setBounds (shelfControls.removeFromLeft (104));
    shelfControls.removeFromLeft (7);
    removeShelfButton.setBounds (shelfControls.removeFromLeft (76));

    auto loop = loopCardBounds.reduced (12);
    loop.removeFromTop (21);
    auto tools = loop.removeFromTop (34);
    snapLabel.setBounds (tools.removeFromLeft (42));
    snapCombo.setBounds (tools.removeFromLeft (78).reduced (2, 3));
    tools.removeFromLeft (8);
    loopLengthLabel.setBounds (tools.removeFromLeft (42));
    loopLengthCombo.setBounds (tools.removeFromLeft (92).reduced (2, 3));
    tools.removeFromLeft (8);
    velocityLabel.setBounds (tools.removeFromLeft (76));
    velocitySlider.setBounds (tools.removeFromLeft (190));
    if (pianoRoll != nullptr)
        pianoRoll->setBounds (loop.reduced (0, 2));

    if (keyboard != nullptr)
        keyboard->setBounds (keyboardCardBounds.reduced (12).withTrimmedTop (24));

    auto device = deviceCardBounds.reduced (14);
    device.removeFromTop (24);
    deviceSummaryLabel.setBounds (device.removeFromTop (62));
    diagnosticLabel.setBounds (device.removeFromBottom (34));
    device.removeFromBottom (6);
    const auto proposalHeight = juce::jlimit (240, 260, device.getHeight() - 180);
    editProposalBounds = device.removeFromBottom (proposalHeight);
    device.removeFromBottom (8);
    if (deviceSelector != nullptr)
        deviceSelector->setBounds (device);

    auto proposal = editProposalBounds.reduced (10);
    proposal.removeFromTop (20);
    editProposalSummaryLabel.setBounds (proposal.removeFromTop (28));
    editProposalDiffLabel.setBounds (proposal.removeFromTop (50));
    proposal.removeFromTop (3);
    auto dynamicsCaptions = proposal.removeFromTop (13);
    const auto dynamicsGap = 4;
    const auto scopeWidth = 112;
    const auto strengthWidth = 66;
    dynamicsScopeLabel.setBounds (dynamicsCaptions.removeFromLeft (scopeWidth));
    dynamicsCaptions.removeFromLeft (dynamicsGap);
    dynamicsStrengthLabel.setBounds (dynamicsCaptions.removeFromLeft (strengthWidth));
    dynamicsCaptions.removeFromLeft (dynamicsGap);
    dynamicsSeedLabel.setBounds (dynamicsCaptions);

    auto dynamicsControls = proposal.removeFromTop (26);
    dynamicsScopeCombo.setBounds (dynamicsControls.removeFromLeft (scopeWidth));
    dynamicsControls.removeFromLeft (dynamicsGap);
    dynamicsStrengthEditor.setBounds (dynamicsControls.removeFromLeft (strengthWidth));
    dynamicsControls.removeFromLeft (dynamicsGap);
    dynamicsSeedEditor.setBounds (dynamicsControls);
    proposal.removeFromTop (4);
    auto previewActions = proposal.removeFromTop (28);
    const auto previewGap = 4;
    const auto previewWidth = (previewActions.getWidth() - previewGap) / 2;
    previewSelectedEditButton.setBounds (previewActions.removeFromLeft (previewWidth));
    previewActions.removeFromLeft (previewGap);
    previewDynamicsButton.setBounds (previewActions);
    proposal.removeFromTop (4);
    auto commandActions = proposal.removeFromTop (26);
    const auto commandGap = 4;
    const auto commandWidth = (commandActions.getWidth() - commandGap) / 2;
    loadCommandButton.setBounds (commandActions.removeFromLeft (commandWidth));
    commandActions.removeFromLeft (commandGap);
    copyHashButton.setBounds (commandActions);
    proposal.removeFromTop (5);
    auto editActions = proposal.removeFromTop (28);
    const auto actionGap = 4;
    const auto actionWidth = (editActions.getWidth() - actionGap * 3) / 4;
    auditionEditProjectButton.setBounds (editActions.removeFromLeft (actionWidth));
    editActions.removeFromLeft (actionGap);
    auditionEditCandidateButton.setBounds (editActions.removeFromLeft (actionWidth));
    editActions.removeFromLeft (actionGap);
    applyEditButton.setBounds (editActions.removeFromLeft (actionWidth));
    editActions.removeFromLeft (actionGap);
    rejectEditButton.setBounds (editActions);
}
} // namespace resonance

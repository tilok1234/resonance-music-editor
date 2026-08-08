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
    configureControls();
    initialiseAudioAndPlugin();
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

    for (auto* button : { &newButton, &openButton, &saveButton, &undoButton, &redoButton,
                          &playButton, &stopButton, &panicButton, &pluginEditorButton,
                          &auditionProjectSoundButton, &captureSoundButton,
                          &auditionCandidateButton, &applySoundButton, &rejectSoundButton,
                          &previewSelectedEditButton, &auditionEditProjectButton,
                          &auditionEditCandidateButton, &applyEditButton, &rejectEditButton })
        addAndMakeVisible (*button);

    playButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.55f));
    panicButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.55f));
    saveButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    captureSoundButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    applySoundButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.55f));
    rejectSoundButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.65f));
    previewSelectedEditButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    auditionEditCandidateButton.setColour (juce::TextButton::buttonColourId, secondary.darker (0.55f));
    applyEditButton.setColour (juce::TextButton::buttonColourId, primary.darker (0.55f));
    rejectEditButton.setColour (juce::TextButton::buttonColourId, danger.darker (0.65f));

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
    auditionEditProjectButton.onClick = [this] { auditionEditProject(); };
    auditionEditCandidateButton.onClick = [this] { auditionEditCandidate(); };
    applyEditButton.onClick = [this] { applyEditPreview(); };
    rejectEditButton.onClick = [this] { rejectEditPreview(); };

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
    for (auto* label : { &bpmLabel, &gainLabel, &snapLabel, &loopLengthLabel, &velocityLabel })
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
        project.beginUndoTransaction ("Change note velocity");
    };
    velocitySlider.onDragEnd = [this] { velocityGestureActive = false; };
    velocitySlider.onValueChange = [this]
    {
        if (refreshingProjectControls || pianoRoll == nullptr)
            return;

        auto selected = project.findNote (pianoRoll->getSelectedNote());
        if (selected.has_value())
        {
            if (! velocityGestureActive)
                project.beginUndoTransaction ("Change note velocity");
            selected->velocity = juce::roundToInt (velocitySlider.getValue());
            project.updateNote (*selected);
        }
    };
    addAndMakeVisible (velocitySlider);

    pianoRoll = std::make_unique<PianoRoll> (project);
    pianoRoll->setSelectionChangedCallback ([this] (const juce::String& id) { selectedNoteChanged (id); });
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
    juce::String loadError;
    auto plugin = pluginFormats.createPluginInstance (pluginRecord.description, rate, blockSize, loadError);

    if (plugin == nullptr)
    {
        startupError = "Accepted plug-in could not be loaded: " + loadError;
        updateStatus();
        return;
    }

    if (plugin->getParameters().size() != pluginRecord.expectedParameterCount)
    {
        startupError = "Loaded plug-in parameter count differs from the accepted scan; rescan is required";
        updateStatus();
        return;
    }

    engine.setPlugin (std::move (plugin));
    project.setPluginMetadata (pluginRecord.identifier,
                               pluginRecord.description.name,
                               pluginRecord.description.manufacturerName,
                               pluginRecord.description.version);

    const auto stateResult = engine.capturePluginState (initialPluginState);
    if (stateResult.failed())
        startupError = "Surge state capture failed: " + stateResult.getErrorMessage();
    else
    {
        project.setPluginState (initialPluginState);
        acceptedLiveSoundSha256 = project.getPluginStateSha256();
        auditionedSoundSha256 = acceptedLiveSoundSha256;
    }

    if (rate == 44100.0 || rate == 48000.0 || rate == 88200.0 || rate == 96000.0)
        project.setSampleRate (juce::roundToInt (rate));
    project.markClean();

    keyboardState.addListener (&engine.getMidiCollector());
    keyboardListenerRegistered = true;
    deviceManager.addMidiInputDeviceCallback ({}, &engine.getMidiCollector());
    midiCallbackRegistered = true;
    deviceManager.addAudioCallback (&engine);
    audioCallbackRegistered = true;

    if (initialPluginState.getSize() > 0)
    {
        juce::MemoryBlock preparedLiveState;
        if (engine.restorePluginState (initialPluginState, &preparedLiveState).wasOk())
        {
            acceptedLiveSoundSha256 = juce::SHA256 (preparedLiveState).toHexString();
            auditionedSoundSha256 = acceptedLiveSoundSha256;
        }
    }

    trackNameLabel.setText ("01  /  " + pluginRecord.description.name, juce::dontSendNotification);
    refreshProjectControls();
    updateStatus();
}

void MainEditorComponent::openPluginEditor()
{
    auto* plugin = engine.getPlugin();
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

    juce::MemoryBlock state;
    const auto result = engine.capturePluginState (state);
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
    if (! soundCandidate.has_value() || hasPendingEditPreview())
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
    auditionedSoundSha256 = acceptedLiveSoundSha256;
    clearSoundCandidate();
    projectStatusMessage = "SOUND APPLIED  /  UNDO RESTORES THE PREVIOUS SOUND";
    refreshProjectControls();
}

void MainEditorComponent::rejectSoundCandidate()
{
    if (! soundCandidate.has_value() || hasPendingEditPreview())
        return;

    if (! restoreProjectSound ("B REJECTED  /  A RESTORED"))
        return;

    clearSoundCandidate();
    projectStatusMessage = "B REJECTED  /  PROJECT SOUND UNCHANGED";
    refreshSoundControls();
}

void MainEditorComponent::performUndoRedo (bool redo)
{
    const auto previousHash = project.getPluginStateSha256();
    const auto changed = redo ? project.redo() : project.undo();
    if (! changed)
        return;

    if (! previousHash.equalsIgnoreCase (project.getPluginStateSha256()))
    {
        clearSoundCandidate();
        restoreProjectSound (redo
                                 ? "REDO RESTORED THE ACCEPTED SOUND"
                                 : "UNDO RESTORED THE PREVIOUS SOUND");
    }
    else
    {
        refreshProjectControls();
    }
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

    juce::MemoryBlock liveState;
    const auto restore = engine.restorePluginState (snapshot.state, &liveState);
    if (restore.failed())
    {
        showError ("Could not restore Surge XT", restore.getErrorMessage());
        return false;
    }

    const auto restoredLiveHash = juce::SHA256 (liveState).toHexString();
    if (liveStateSha256 != nullptr)
        *liveStateSha256 = restoredLiveHash;
    auditionedSoundSha256 = restoredLiveHash;
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

    return restoreSoundSnapshot (accepted, actionLabel, &acceptedLiveSoundSha256);
}

bool MainEditorComponent::hasUncapturedLiveSoundState()
{
    if (engine.getPlugin() == nullptr)
        return false;

    juce::MemoryBlock liveState;
    if (engine.capturePluginState (liveState).failed() || liveState.getSize() == 0)
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
    candidateLiveSoundSha256.clear();
    soundNameEditor.setText ("Captured sound", false);
}

void MainEditorComponent::refreshSoundControls()
{
    const auto ready = engine.getPlugin() != nullptr;
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

void MainEditorComponent::previewSelectedNoteEdit()
{
    if (soundCandidate.has_value())
    {
        projectStatusMessage = "APPLY OR REJECT SOUND B BEFORE PREVIEWING A NOTE EDIT";
        updateStatus();
        return;
    }

    const auto selected = pianoRoll != nullptr
                              ? project.findNote (pianoRoll->getSelectedNote())
                              : std::nullopt;
    if (! selected.has_value())
    {
        projectStatusMessage = "SELECT A NOTE BEFORE CREATING A PROPOSAL";
        updateStatus();
        return;
    }
    if (selected->midiNote >= 127)
    {
        projectStatusMessage = "THE SELECTED NOTE IS ALREADY AT THE HIGHEST MIDI PITCH";
        updateStatus();
        return;
    }

    if (hasPendingEditPreview())
        clearEditPreview (true);

    auto after = *selected;
    ++after.midiNote;

    EditCommand command;
    command.projectContentSha256 = project.getContentSha256();
    command.summary = "Transpose " + selected->id + " up one semitone";
    command.changes.push_back ({ NoteEditAction::update, selected->id, after });

    EditCommandPreview proposed;
    const auto result = createEditCommandPreview (command, project, proposed);
    if (result.failed())
    {
        projectStatusMessage = "NOTE PROPOSAL ERROR  /  " + result.getErrorMessage();
        updateStatus();
        showError ("Could not preview note edit", result.getErrorMessage());
        return;
    }

    editPreview.emplace (std::move (proposed));
    auditioningEditCandidate = false;
    engine.setSequence (project.createSequenceSnapshot());
    if (pianoRoll != nullptr)
        pianoRoll->setEditPreview (editPreview->noteDiffs, false);
    projectStatusMessage = "NOTE B READY  /  AUDITION A-B BEFORE APPLY";
    refreshProjectControls();
}

void MainEditorComponent::auditionEditProject()
{
    if (! hasPendingEditPreview())
        return;

    engine.setSequence (project.createSequenceSnapshot());
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

    engine.setSequence (candidate->createSequenceSnapshot());
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
        engine.setSequence (project.createSequenceSnapshot());
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
    engine.setSequence (project.createSequenceSnapshot());
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
        engine.setSequence (project.createSequenceSnapshot());
    refreshSoundControls();
    refreshEditPreviewControls();
}

void MainEditorComponent::refreshEditPreviewControls()
{
    const auto pending = hasPendingEditPreview();
    const auto selected = pianoRoll != nullptr
                              ? project.findNote (pianoRoll->getSelectedNote())
                              : std::nullopt;
    const auto ready = engine.getPlugin() != nullptr;

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
                detail += "  " + noteName (first.before->midiNote);
            if (first.after.has_value())
                detail += (first.before.has_value() ? "  ->  " : "  +  ")
                          + noteName (first.after->midiNote);
        }
        editProposalDiffLabel.setText (counts + "\n" + detail
                                           + "\nA " + shortStateHash (editPreview->beforeContentSha256)
                                           + "  /  B " + shortStateHash (editPreview->afterContentSha256),
                                       juce::dontSendNotification);
        editProposalDiffLabel.setTooltip ("Accepted A: " + editPreview->beforeContentSha256
                                          + "\nCandidate B: " + editPreview->afterContentSha256);
    }
    else if (selected.has_value())
    {
        editProposalSummaryLabel.setText ("READY  /  " + selected->id.toUpperCase()
                                              + "  " + noteName (selected->midiNote),
                                          juce::dontSendNotification);
        editProposalSummaryLabel.setColour (juce::Label::textColourId, textMain);
        editProposalDiffLabel.setText ("Preview selected +1 creates candidate B.\n"
                                       "The accepted song stays unchanged until Apply.",
                                       juce::dontSendNotification);
        editProposalDiffLabel.setTooltip ({});
    }
    else
    {
        editProposalSummaryLabel.setText ("SELECT A NOTE TO PROPOSE AN EDIT",
                                          juce::dontSendNotification);
        editProposalSummaryLabel.setColour (juce::Label::textColourId, textMuted);
        editProposalDiffLabel.setText ("Candidate notes are previewed separately.\n"
                                       "Audition A/B, then Apply or Reject.",
                                       juce::dontSendNotification);
        editProposalDiffLabel.setTooltip ({});
    }

    previewSelectedEditButton.setEnabled (ready && ! pending && ! soundCandidate.has_value()
                                          && selected.has_value() && selected->midiNote < 127);
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
    if (applyingEditPreview)
        return;

    if (hasPendingEditPreview()
        && ! project.getContentSha256().equalsIgnoreCase (editPreview->beforeContentSha256))
    {
        editPreview.reset();
        auditioningEditCandidate = false;
        if (pianoRoll != nullptr)
            pianoRoll->clearEditPreview();
        projectStatusMessage = "NOTE PROPOSAL STALE  /  ACTIVE PROJECT CHANGED";
    }

    engine.setBpm (project.getTempoBpm());
    engine.setSequence (project.createSequenceSnapshot());

    if (pianoRoll != nullptr && pianoRoll->getSelectedNote().isNotEmpty()
        && ! project.findNote (pianoRoll->getSelectedNote()).has_value())
        pianoRoll->setSelectedNote ({});

    refreshProjectControls();
    repaint (loopCardBounds);
}

void MainEditorComponent::refreshProjectControls()
{
    const juce::ScopedValueSetter<bool> refreshing (refreshingProjectControls, true);
    projectNameLabel.setText (project.getTitle() + (project.isDirty() ? "  *" : ""),
                              juce::dontSendNotification);
    projectNameLabel.setTooltip (currentProjectFile == juce::File()
                                     ? "Unsaved project"
                                     : currentProjectFile.getFullPathName());
    bpmSlider.setValue (project.getTempoBpm(), juce::dontSendNotification);
    snapCombo.setSelectedId (snapComboId (project.getSnapBeats()), juce::dontSendNotification);
    loopLengthCombo.setSelectedId (loopComboId (project.getLoopLengthBeats()), juce::dontSendNotification);
    trackMetaLabel.setText (pluginRecord.description.manufacturerName + "  /  VST3 "
                            + pluginRecord.description.version + "  /  "
                            + juce::String (pluginRecord.expectedParameterCount) + " parameters  /  SOUND  "
                            + project.getPluginSoundName(),
                            juce::dontSendNotification);

    undoButton.setEnabled (project.canUndo());
    redoButton.setEnabled (project.canRedo());
    undoButton.setTooltip (project.canUndo() ? "Undo " + project.getUndoDescription() : "Nothing to undo");
    redoButton.setTooltip (project.canRedo() ? "Redo " + project.getRedoDescription() : "Nothing to redo");

    auto selected = pianoRoll != nullptr ? project.findNote (pianoRoll->getSelectedNote()) : std::nullopt;
    velocitySlider.setEnabled (selected.has_value());
    velocityLabel.setText (selected.has_value() ? "VELOCITY" : "SELECT NOTE", juce::dontSendNotification);
    if (selected.has_value())
        velocitySlider.setValue (selected->velocity, juce::dontSendNotification);

    refreshSoundControls();
    refreshEditPreviewControls();
}

void MainEditorComponent::selectedNoteChanged (const juce::String&)
{
    refreshProjectControls();
}

void MainEditorComponent::startNewProject()
{
    engine.stopAndRewind();
    pluginEditorWindow.reset();
    clearEditPreview (false);
    clearSoundCandidate();
    if (initialPluginState.getSize() > 0)
    {
        juce::MemoryBlock liveState;
        const auto restore = engine.restorePluginState (initialPluginState, &liveState);
        if (restore.failed())
        {
            showError ("Could not reset Surge XT", restore.getErrorMessage());
            return;
        }
        acceptedLiveSoundSha256 = juce::SHA256 (liveState).toHexString();
    }

    project.resetToStarter();
    project.setPluginMetadata (pluginRecord.identifier,
                               pluginRecord.description.name,
                               pluginRecord.description.manufacturerName,
                               pluginRecord.description.version);
    if (initialPluginState.getSize() > 0)
    {
        project.setPluginState (initialPluginState);
        auditionedSoundSha256 = acceptedLiveSoundSha256.isNotEmpty()
                                    ? acceptedLiveSoundSha256
                                    : project.getPluginStateSha256();
    }
    currentProjectFile = {};
    if (pianoRoll != nullptr)
        pianoRoll->setSelectedNote ({});
    refreshProjectControls();
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

    const auto identifierMatches = vst3IdentifiersAreCompatible (candidate.getPluginIdentifier(),
                                                                  pluginRecord.identifier,
                                                                  pluginRecord.description.uniqueId);
    const auto nameMatches = candidate.getPluginName().equalsIgnoreCase (pluginRecord.description.name);
    if (! identifierMatches || ! nameMatches)
    {
        showError ("Different instrument",
                   "The saved song's VST3 identity does not match the active instrument. Saved: "
                       + candidate.getPluginIdentifier() + ". Active: " + pluginRecord.identifier + ".");
        return;
    }

    juce::MemoryBlock state;
    const auto stateResult = candidate.getPluginState (state);
    if (stateResult.failed())
    {
        showError ("Invalid Surge state", stateResult.getErrorMessage());
        return;
    }

    engine.stopAndRewind();
    pluginEditorWindow.reset();
    clearEditPreview (false);
    juce::MemoryBlock liveState;
    const auto restoreResult = engine.restorePluginState (state, &liveState);
    if (restoreResult.failed())
    {
        showError ("Could not restore Surge XT", restoreResult.getErrorMessage());
        return;
    }

    project.replaceWith (candidate);
    currentProjectFile = file;
    project.markClean();
    clearSoundCandidate();
    acceptedLiveSoundSha256 = juce::SHA256 (liveState).toHexString();
    auditionedSoundSha256 = acceptedLiveSoundSha256;
    projectStatusMessage = "OPENED  /  " + file.getFileName();
    if (pianoRoll != nullptr)
        pianoRoll->setSelectedNote ({});
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

    PluginSoundSnapshot acceptedSound;
    const auto stateResult = project.getPluginSoundSnapshot (acceptedSound);
    if (stateResult.failed())
    {
        showError ("Could not read the accepted project sound", stateResult.getErrorMessage());
        return;
    }

    project.setPluginMetadata (pluginRecord.identifier,
                               pluginRecord.description.name,
                               pluginRecord.description.manufacturerName,
                               pluginRecord.description.version);
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
    const auto baselineCapture = engine.capturePluginState (baselineLiveState);
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

    pianoRoll->setSelectedNote (notes.front().id);
    previewSelectedNoteEdit();
}

juce::var MainEditorComponent::runM5WorkflowSelfTest()
{
    auto* resultObject = new juce::DynamicObject();
    juce::var result (resultObject);
    resultObject->setProperty ("schemaVersion", 1);

    clearEditPreview (true);
    clearSoundCandidate();
    project.markClean();

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
                        && closeAcceptedWithoutWarning
                        && engine.getInvalidSampleCount() == 0
                        && engine.getProcessorExceptionCount() == 0;
    resultObject->setProperty ("passed", passed);
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
    displayedLeftPeak = juce::jmax (engine.getLeftPeak(), displayedLeftPeak * 0.88f);
    displayedRightPeak = juce::jmax (engine.getRightPeak(), displayedRightPeak * 0.88f);
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
    if (engine.getPlugin() == nullptr)
    {
        status = "PLUGIN OFFLINE";
        statusColour = danger;
    }
    else if (device == nullptr || ! engine.isPrepared())
    {
        status = "CHOOSE AUDIO OUTPUT";
        statusColour = warning;
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
    else if (projectStatusMessage.isNotEmpty())
        diagnostic = projectStatusMessage;

    diagnosticLabel.setText (diagnostic, juce::dontSendNotification);
    diagnosticLabel.setColour (juce::Label::textColourId,
                               (startupError.isNotEmpty() || invalid > 0 || exceptions > 0) ? danger : textMuted);

    const auto ready = engine.getPlugin() != nullptr && device != nullptr && engine.isPrepared();
    playButton.setEnabled (ready);
    stopButton.setEnabled (engine.getPlugin() != nullptr);
    panicButton.setEnabled (engine.getPlugin() != nullptr);
    saveButton.setEnabled (engine.getPlugin() != nullptr);
    pluginEditorButton.setEnabled (engine.getPlugin() != nullptr && pluginRecord.hasEditor);
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
    trackCardBounds = body.removeFromTop (132);
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
    const auto proposalHeight = juce::jlimit (170, 190, device.getHeight() - 200);
    editProposalBounds = device.removeFromBottom (proposalHeight);
    device.removeFromBottom (8);
    if (deviceSelector != nullptr)
        deviceSelector->setBounds (device);

    auto proposal = editProposalBounds.reduced (10);
    proposal.removeFromTop (20);
    editProposalSummaryLabel.setBounds (proposal.removeFromTop (28));
    editProposalDiffLabel.setBounds (proposal.removeFromTop (50));
    proposal.removeFromTop (4);
    previewSelectedEditButton.setBounds (proposal.removeFromTop (28));
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

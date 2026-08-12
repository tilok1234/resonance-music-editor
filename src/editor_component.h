#pragma once

#include <JuceHeader.h>

#include "edit_command.h"
#include "known_plugin.h"
#include "piano_roll.h"
#include "realtime_engine.h"
#include "song_project.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace resonance
{
class MainEditorComponent final : public juce::Component,
                                  private juce::Timer,
                                  private juce::ChangeListener
{
public:
    MainEditorComponent (juce::File inventoryFile,
                         juce::File quarantineFile,
                         juce::PropertiesFile* settings);
    ~MainEditorComponent() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void requestClose (std::function<void()> closeAction);
    juce::var runM4WorkflowSelfTest (const juce::File& projectFile);
    juce::var runM5WorkflowSelfTest();
    juce::var runM6AuthoringSelfTest (const juce::File& projectFile);
    juce::var runCommandLoadSelfTest();
    bool openProjectForSnapshot (const juce::File& projectFile);
    void prepareM5PreviewForSnapshot();

private:
    class PluginEditorWindow;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void initialiseAudioAndPlugin();
    void configureControls();
    juce::AudioPluginInstance* getActivePlugin() const noexcept;
    bool canChangeTrackContext();
    void selectTrack (int trackIndex);
    void addInstrumentTrack();
    void removeActiveTrack();
    void moveActiveTrack (int offset);
    juce::Result synchronisePluginSlotsFromProject (bool forceRestore = false);
    juce::Result restoreRuntimeSlotsFromProject (
        const SongProject& source,
        std::array<juce::String, SongProject::maxProjectTracks>& liveStateHashes);
    void publishProjectMixerSnapshot (const SequenceSnapshot* activeTrackOverride = nullptr);
    void updateActiveSoundTracking();
    void openPluginEditor();
    void captureSoundCandidate();
    void auditionProjectSound();
    void auditionSoundCandidate();
    void applySoundCandidate();
    void rejectSoundCandidate();
    void performUndoRedo (bool redo);
    bool restoreSoundSnapshot (const PluginSoundSnapshot& snapshot,
                               const juce::String& actionLabel,
                               juce::String* liveStateSha256 = nullptr);
    bool restoreProjectSound (const juce::String& actionLabel);
    bool hasUncapturedLiveSoundState();
    void clearSoundCandidate();
    void refreshSoundControls();
    void previewSelectedNoteEdit();
    void previewVelocityVariation();
    juce::Result readVelocityVariationControls (SeededVelocityVariation& variation) const;
    void chooseEditCommandFile();
    juce::Result loadEditCommandFile (const juce::File& commandFile, bool reportFailure = true);
    void copyProjectContentHash();
    juce::Result installEditPreview (EditCommand command,
                                     const juce::String& readyStatus,
                                     bool reportFailure = true);
    void auditionEditProject();
    void auditionEditCandidate();
    void applyEditPreview();
    void rejectEditPreview();
    void clearEditPreview (bool publishActiveSequence);
    void refreshEditPreviewControls();
    bool hasPendingEditPreview() const noexcept;
    void updateStatus();
    void saveSettings();
    void drawCard (juce::Graphics&, juce::Rectangle<int>) const;

    void projectChanged();
    void refreshProjectControls();
    void selectedNoteChanged (const juce::String& noteId);
    void startNewProject();
    void chooseProjectToOpen();
    void openProjectFile (const juce::File& file);
    void saveProject();
    void chooseProjectSaveLocation();
    void saveProjectToFile (juce::File file);
    void confirmDiscardIfNeeded (std::function<void()> action);
    void showError (const juce::String& title, const juce::String& message);

    juce::File inventoryPath;
    juce::File quarantinePath;
    juce::PropertiesFile* settingsFile = nullptr;
    KnownPluginRecord pluginRecord;
    juce::String startupError;
    juce::String deviceStartupError;
    juce::String projectStatusMessage;

    juce::LookAndFeel_V4 lookAndFeel;
    juce::AudioDeviceManager deviceManager;
    RealtimeEngine engine;
    SongProject project;
    juce::AudioPluginFormatManager pluginFormats;
    juce::MidiKeyboardState keyboardState;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard;
    std::unique_ptr<PluginEditorWindow> pluginEditorWindow;
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    std::array<juce::MemoryBlock, SongProject::maxProjectTracks> initialPluginStates;
    std::array<juce::String, SongProject::maxProjectTracks> slotProjectStateSha256;
    std::array<juce::String, SongProject::maxProjectTracks> slotAcceptedLiveSoundSha256;
    std::optional<PluginSoundSnapshot> soundCandidate;
    std::optional<EditCommandPreview> editPreview;
    juce::String acceptedLiveSoundSha256;
    juce::String candidateLiveSoundSha256;
    juce::String auditionedSoundSha256;
    juce::String activeSoundTrackId;
    juce::String soundCandidateTrackId;
    juce::String pluginEditorTrackId;
    juce::String runtimeProjectSyncError;
    int pluginEditorTrackIndex = -1;
    juce::File currentProjectFile;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label projectNameLabel;
    juce::Label statusLabel;
    juce::Label trackNameLabel;
    juce::Label trackMetaLabel;
    juce::Label soundWorkflowLabel;
    juce::Label transportPositionLabel;
    juce::Label deviceSummaryLabel;
    juce::Label diagnosticLabel;
    juce::Label editProposalSummaryLabel;
    juce::Label editProposalDiffLabel;
    juce::Label dynamicsScopeLabel;
    juce::Label dynamicsStrengthLabel;
    juce::Label dynamicsSeedLabel;
    juce::TextButton newButton { "New" };
    juce::TextButton openButton { "Open" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton playButton { "Play loop" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton panicButton { "Panic" };
    juce::TextButton pluginEditorButton { "Open Surge XT" };
    juce::TextButton auditionProjectSoundButton { "Audition A" };
    juce::TextButton captureSoundButton { "Capture B" };
    juce::TextButton auditionCandidateButton { "Audition B" };
    juce::TextButton applySoundButton { "Apply B" };
    juce::TextButton rejectSoundButton { "Reject B" };
    juce::TextButton previewSelectedEditButton { "Selected +1" };
    juce::TextButton previewDynamicsButton { "Preview dynamics" };
    juce::TextButton loadCommandButton { "Load command" };
    juce::TextButton copyHashButton { "Copy hash" };
    juce::TextButton auditionEditProjectButton { "Audition A" };
    juce::TextButton auditionEditCandidateButton { "Audition B" };
    juce::TextButton applyEditButton { "Apply" };
    juce::TextButton rejectEditButton { "Reject" };
    juce::TextButton addTrackButton { "+ Track" };
    juce::TextButton removeTrackButton { "- Track" };
    juce::TextButton moveTrackLeftButton { "<" };
    juce::TextButton moveTrackRightButton { ">" };
    juce::ToggleButton trackMuteButton { "Mute" };
    juce::ToggleButton trackSoloButton { "Solo" };
    juce::TextEditor soundNameEditor;
    juce::ComboBox trackSelector;
    juce::ComboBox dynamicsScopeCombo;
    juce::TextEditor dynamicsStrengthEditor;
    juce::TextEditor dynamicsSeedEditor;
    juce::Slider bpmSlider;
    juce::Slider gainSlider;
    juce::Slider velocitySlider;
    juce::Slider trackGainSlider;
    juce::Slider trackPanSlider;
    juce::ComboBox snapCombo;
    juce::ComboBox loopLengthCombo;
    juce::Label bpmLabel;
    juce::Label gainLabel;
    juce::Label snapLabel;
    juce::Label loopLengthLabel;
    juce::Label velocityLabel;
    juce::Label trackGainLabel;
    juce::Label trackPanLabel;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> transportCardBounds;
    juce::Rectangle<int> trackCardBounds;
    juce::Rectangle<int> loopCardBounds;
    juce::Rectangle<int> keyboardCardBounds;
    juce::Rectangle<int> deviceCardBounds;
    juce::Rectangle<int> editProposalBounds;

    float displayedLeftPeak = 0.0f;
    float displayedRightPeak = 0.0f;
    juce::int64 lastClipCount = 0;
    bool audioCallbackRegistered = false;
    bool midiCallbackRegistered = false;
    bool keyboardListenerRegistered = false;
    bool refreshingProjectControls = false;
    bool bpmGestureActive = false;
    bool velocityGestureActive = false;
    bool trackGainGestureActive = false;
    bool trackPanGestureActive = false;
    bool auditioningEditCandidate = false;
    bool applyingEditPreview = false;
    bool suppressProjectChanges = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainEditorComponent)
};
} // namespace resonance

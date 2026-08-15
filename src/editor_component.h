#pragma once

#include <JuceHeader.h>

#include "edit_command.h"
#include "known_plugin.h"
#include "piano_roll.h"
#include "realtime_engine.h"
#include "song_project.h"
#include "sound_shelf.h"

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
    juce::var runSelectionSelfTest();
    juce::var runSoundShelfSelfTest (const juce::File& alternateProjectFile);
    juce::var runAudioProbeSelfTest (const juce::File& projectFile);
    juce::var renderProjectToWav (const juce::File& projectFile,
                                  const juce::File& wavFile,
                                  int repeats,
                                  double tailSeconds);
    bool openProjectFromCommandLine (const juce::File& projectFile);
    void prepareM5PreviewForSnapshot();

private:
    class PluginEditorWindow;
    class SettingsWindow;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void initialiseAudioAndPlugin();
    void configureControls();
    void configureTrackStrips();
    void refreshTrackStrips();
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
    void openSettingsWindow();
    void toggleKeyboard();
    void toggleAdvancedControls();
    void applyAdvancedControlVisibility();
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
    void refreshShelfControls();
    void saveSoundToShelf();
    juce::Result loadSoundFromShelf (bool reportFailure = true);
    void removeSoundFromShelf();
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
    juce::String velocityTransactionName() const;
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
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    std::array<juce::MemoryBlock, SongProject::maxProjectTracks> initialPluginStates;
    std::array<juce::String, SongProject::maxProjectTracks> slotProjectStateSha256;
    std::array<juce::String, SongProject::maxProjectTracks> slotAcceptedLiveSoundSha256;
    SoundShelf soundShelf;
    juce::File soundShelfPath;
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
    juce::TextButton settingsButton { "Audio" };
    juce::TextButton keyboardToggleButton { "Keys" };
    juce::TextButton advancedToggleButton { "Advanced" };
    juce::TextButton auditionProjectSoundButton { "Audition A" };
    juce::TextButton captureSoundButton { "Capture B" };
    juce::TextButton auditionCandidateButton { "Audition B" };
    juce::TextButton applySoundButton { "Apply B" };
    juce::TextButton rejectSoundButton { "Reject B" };
    juce::TextButton previewSelectedEditButton { "Selected +1" };
    juce::TextButton previewDynamicsButton { "Preview dynamics" };
    juce::TextButton loadCommandButton { "Load command" };
    juce::TextButton copyHashButton { "Copy hash" };
    juce::TextButton loadShelfButton { "Load sound" };
    juce::TextButton saveShelfButton { "Save to shelf" };
    juce::TextButton removeShelfButton { "Remove" };
    juce::ComboBox shelfCombo;
    juce::Label shelfLabel;
    juce::TextButton auditionEditProjectButton { "Audition A" };
    juce::TextButton auditionEditCandidateButton { "Audition B" };
    juce::TextButton applyEditButton { "Apply" };
    juce::TextButton rejectEditButton { "Reject" };
    juce::TextButton addTrackButton { "+ Track" };
    juce::TextButton removeTrackButton { "- Track" };
    juce::TextButton moveTrackLeftButton { "<" };
    juce::TextButton moveTrackRightButton { ">" };
    juce::TextEditor soundNameEditor;
    juce::ComboBox dynamicsScopeCombo;
    juce::TextEditor dynamicsStrengthEditor;
    juce::TextEditor dynamicsSeedEditor;
    juce::Slider bpmSlider;
    juce::Slider gainSlider;
    juce::Slider velocitySlider;
    juce::ComboBox snapCombo;
    juce::ComboBox loopLengthCombo;
    juce::Label bpmLabel;
    juce::Label gainLabel;
    juce::Label snapLabel;
    juce::Label loopLengthLabel;
    juce::Label velocityLabel;

    // One strip per project track. The mixer previously showed only the selected
    // track, so a four-track balance could not be seen or set without clicking
    // through every track in turn.
    struct TrackStrip
    {
        juce::TextButton selectButton;
        juce::Slider gainSlider;
        juce::Slider panSlider;
        juce::ToggleButton muteButton { "M" };
        juce::ToggleButton soloButton { "S" };
    };
    std::array<TrackStrip, SongProject::maxProjectTracks> trackStrips;
    std::array<juce::Rectangle<int>, SongProject::maxProjectTracks> trackStripBounds;
    std::array<float, SongProject::maxProjectTracks> displayedTrackPeaks {};
    int visibleTrackStripCount = 0;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> transportCardBounds;
    juce::Rectangle<int> trackCardBounds;
    juce::Rectangle<int> soundCardBounds;
    juce::Rectangle<int> loopCardBounds;
    juce::Rectangle<int> keyboardCardBounds;
    juce::Rectangle<int> footerBounds;
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
    // Layout state: the on-screen keyboard and the resolver inputs are both
    // occasional, so neither holds space by default.
    bool keyboardVisible = false;
    bool advancedControlsVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainEditorComponent)
};
} // namespace resonance

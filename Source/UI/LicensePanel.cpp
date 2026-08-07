#include "LicensePanel.h"
#include "AfterimageFonts.h"
#include "AfterimageTooltips.h"

//==============================================================================
class LicensePanel::ActivationOverlay : public juce::Component
{
public:
    explicit ActivationOverlay (afterimage::licensing::LicenseManager& manager)
        : manager_ (manager)
    {
        title_.setText ("LICENSE", juce::dontSendNotification);
        title_.setFont (AfterimageFonts::get (AfterimageFontRole::Wordmark).withHeight (18.0f));
        title_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
        title_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title_);

        status_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        status_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::accentCyan());
        status_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status_);

        hint_.setText ("Paste an AFTERIMAGE-LICENSE-1 document, or import a license file.",
                       juce::dontSendNotification);
        hint_.setFont (AfterimageFonts::get (AfterimageFontRole::Caption));
        hint_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textMuted());
        hint_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (hint_);

        error_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        error_.setColour (juce::Label::textColourId, juce::Colour (0xffe08a7a));
        error_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (error_);

        pasteBox_.setMultiLine (true);
        pasteBox_.setReturnKeyStartsNewLine (true);
        pasteBox_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        pasteBox_.setTextToShowWhenEmpty ("Paste AFTERIMAGE-LICENSE-1 text here",
                                          AfterimageLookAndFeel::textMuted());
        pasteBox_.setColour (juce::TextEditor::backgroundColourId,
                             AfterimageLookAndFeel::background().brighter (0.04f));
        pasteBox_.setColour (juce::TextEditor::outlineColourId, AfterimageLookAndFeel::panelEdge());
        pasteBox_.setColour (juce::TextEditor::focusedOutlineColourId,
                             AfterimageLookAndFeel::accentCyan().withAlpha (0.55f));
        pasteBox_.setColour (juce::TextEditor::textColourId, AfterimageLookAndFeel::textPrimary());
        pasteBox_.setColour (juce::TextEditor::highlightColourId,
                             AfterimageLookAndFeel::accentViolet().withAlpha (0.35f));
        pasteBox_.setColour (juce::TextEditor::highlightedTextColourId,
                             AfterimageLookAndFeel::textPrimary());
        pasteBox_.setColour (juce::CaretComponent::caretColourId,
                             AfterimageLookAndFeel::accentCyan());
        pasteBox_.setIndents (10, 8);
        addAndMakeVisible (pasteBox_);

        auto style = [] (juce::TextButton& b, bool primary)
        {
            b.setColour (juce::TextButton::buttonColourId, AfterimageLookAndFeel::panel());
            b.setColour (juce::TextButton::textColourOffId,
                         primary ? AfterimageLookAndFeel::accentCyan()
                                 : AfterimageLookAndFeel::textPrimary());
            if (primary)
                b.getProperties().set ("afterimagePrimary", true);
        };
        style (importButton_, false);
        style (activateButton_, true);
        style (deactivateButton_, false);
        style (closeButton_, false);
        addAndMakeVisible (importButton_);
        addAndMakeVisible (activateButton_);
        addAndMakeVisible (deactivateButton_);
        addAndMakeVisible (closeButton_);

        importButton_.onClick = [this] { doImport(); };
        activateButton_.onClick = [this] { doPaste(); };
        deactivateButton_.onClick = [this] { doDeactivate(); };
        closeButton_.onClick = [this]
        {
            if (onClose)
                onClose();
        };

        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.58f));

        auto panel = panelBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (panel.translated (0.0f, 2.0f), 12.0f);

        juce::ColourGradient gloss (AfterimageLookAndFeel::panel().brighter (0.05f),
                                    panel.getX(), panel.getY(),
                                    AfterimageLookAndFeel::glassFill(),
                                    panel.getX(), panel.getBottom(),
                                    false);
        g.setGradientFill (gloss);
        g.fillRoundedRectangle (panel, 12.0f);
        g.setColour (AfterimageLookAndFeel::glassEdge().withAlpha (0.4f));
        g.drawRoundedRectangle (panel.reduced (0.5f), 12.0f, 1.1f);
    }

    void resized() override
    {
        auto r = panelBounds().reduced (20, 18);
        title_.setBounds (r.removeFromTop (22));
        r.removeFromTop (8);
        status_.setBounds (r.removeFromTop (18));
        r.removeFromTop (6);
        hint_.setBounds (r.removeFromTop (16));
        r.removeFromTop (10);
        error_.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);

        auto buttons = r.removeFromBottom (34);
        const int gap = 8;
        closeButton_.setBounds (buttons.removeFromRight (76));
        buttons.removeFromRight (gap);
        deactivateButton_.setBounds (buttons.removeFromRight (96));
        buttons.removeFromRight (gap);
        activateButton_.setBounds (buttons.removeFromRight (88));
        buttons.removeFromRight (gap);
        importButton_.setBounds (buttons);

        r.removeFromBottom (12);
        pasteBox_.setBounds (r);
    }

    void refresh()
    {
        status_.setText (manager_.getStatusMessage(), juce::dontSendNotification);
        const bool licensed = manager_.isLicensed();
        deactivateButton_.setEnabled (licensed);
        status_.setColour (juce::Label::textColourId,
                           licensed ? AfterimageLookAndFeel::accentCyan()
                                    : AfterimageLookAndFeel::accentWarm());
    }

    std::function<void()> onClose;
    std::function<void()> onChanged;

private:
    juce::Rectangle<int> panelBounds() const
    {
        auto panel = getLocalBounds().reduced (juce::jmax (24, getWidth() / 6),
                                               juce::jmax (40, getHeight() / 6));
        return panel.withSizeKeepingCentre (juce::jmin (440, panel.getWidth()),
                                            juce::jmin (380, panel.getHeight()));
    }

    void doImport()
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Import AFTERIMAGE license", juce::File(), "*.afterimage-license;*.txt");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto results = fc.getResults();
                                  if (results.isEmpty())
                                      return;
                                  auto result = manager_.importLicenseFile (results.getFirst());
                                  error_.setText (result.message, juce::dontSendNotification);
                                  error_.setColour (juce::Label::textColourId,
                                                    result.ok ? AfterimageLookAndFeel::accentCyan()
                                                              : juce::Colour (0xffe08a7a));
                                  refresh();
                                  if (onChanged)
                                      onChanged();
                              });
    }

    void doPaste()
    {
        const auto text = pasteBox_.getText().trim();
        if (text.isEmpty())
        {
            error_.setText ("Paste a license document first.", juce::dontSendNotification);
            error_.setColour (juce::Label::textColourId, juce::Colour (0xffe08a7a));
            return;
        }
        auto result = manager_.activateFromKey (text);
        error_.setText (result.message, juce::dontSendNotification);
        error_.setColour (juce::Label::textColourId,
                          result.ok ? AfterimageLookAndFeel::accentCyan()
                                    : juce::Colour (0xffe08a7a));
        refresh();
        if (onChanged)
            onChanged();
    }

    void doDeactivate()
    {
        auto result = manager_.deactivateLocalLicense();
        error_.setText (result.message, juce::dontSendNotification);
        error_.setColour (juce::Label::textColourId,
                          result.ok ? AfterimageLookAndFeel::accentCyan()
                                    : juce::Colour (0xffe08a7a));
        pasteBox_.clear();
        refresh();
        if (onChanged)
            onChanged();
    }

    afterimage::licensing::LicenseManager& manager_;
    juce::Label title_, status_, hint_, error_;
    juce::TextEditor pasteBox_;
    juce::TextButton importButton_ { "Import License" };
    juce::TextButton activateButton_ { "Activate" };
    juce::TextButton deactivateButton_ { "Deactivate" };
    juce::TextButton closeButton_ { "Close" };
};

//==============================================================================
LicensePanel::LicensePanel (afterimage::licensing::LicenseManager& manager)
    : manager_ (manager)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refreshStatus();
}

LicensePanel::~LicensePanel()
{
    closeOverlay();
}

void LicensePanel::refreshStatus()
{
    using namespace afterimage::licensing;
    chipText_ = manager_.getStatusMessage().toUpperCase();
    const auto status = manager_.getStatus();
    if (status == LicenseStatus::Licensed)
    {
        chipColour_ = AfterimageLookAndFeel::accentCyan();
        setTooltip (afterimage::tooltips::license);
    }
    else if (status == LicenseStatus::Trial)
    {
        chipColour_ = AfterimageLookAndFeel::accentWarm();
        setTooltip (afterimage::tooltips::trial);
    }
    else
    {
        chipColour_ = juce::Colour (0xffe08a7a);
        setTooltip (afterimage::tooltips::license);
    }
    if (overlay_ != nullptr)
        overlay_->refresh();
    repaint();
}

void LicensePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (AfterimageLookAndFeel::panel().withAlpha (0.9f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (chipColour_.withAlpha (0.4f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (chipColour_);
    g.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
    g.drawText (chipText_, getLocalBounds().reduced (8, 0),
                juce::Justification::centredLeft, true);
}

void LicensePanel::resized()
{
    if (overlay_ != nullptr)
        overlay_->setBounds (getParentComponent() != nullptr
                                 ? getParentComponent()->getLocalBounds()
                                 : getLocalBounds());
}

void LicensePanel::mouseUp (const juce::MouseEvent&)
{
    openOverlay();
}

void LicensePanel::openOverlay()
{
    if (overlay_ != nullptr)
        return;

    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    overlay_ = std::make_unique<ActivationOverlay> (manager_);
    overlay_->onClose = [this] { closeOverlay(); };
    overlay_->onChanged = [this]
    {
        refreshStatus();
        if (onStatusChanged)
            onStatusChanged();
    };
    parent->addAndMakeVisible (*overlay_);
    overlay_->setBounds (parent->getLocalBounds());
    overlay_->toFront (true);
}

void LicensePanel::closeOverlay()
{
    if (overlay_ == nullptr)
        return;
    if (auto* parent = overlay_->getParentComponent())
        parent->removeChildComponent (overlay_.get());
    overlay_.reset();
    refreshStatus();
}

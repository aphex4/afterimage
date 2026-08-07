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
        title_.setFont (AfterimageFonts::get (AfterimageFontRole::Wordmark).withHeight (16.0f));
        title_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::textPrimary());
        addAndMakeVisible (title_);

        status_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        status_.setColour (juce::Label::textColourId, AfterimageLookAndFeel::accentCyan());
        addAndMakeVisible (status_);

        error_.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
        error_.setColour (juce::Label::textColourId, juce::Colour (0xffe08a7a));
        addAndMakeVisible (error_);

        pasteBox_.setMultiLine (true);
        pasteBox_.setReturnKeyStartsNewLine (true);
        pasteBox_.setTextToShowWhenEmpty ("Paste AFTERIMAGE-LICENSE-1 text here",
                                          AfterimageLookAndFeel::textMuted());
        pasteBox_.setColour (juce::TextEditor::backgroundColourId, AfterimageLookAndFeel::panel());
        pasteBox_.setColour (juce::TextEditor::outlineColourId, AfterimageLookAndFeel::panelEdge());
        pasteBox_.setColour (juce::TextEditor::textColourId, AfterimageLookAndFeel::textPrimary());
        addAndMakeVisible (pasteBox_);

        auto style = [] (juce::TextButton& b)
        {
            b.setColour (juce::TextButton::buttonColourId, AfterimageLookAndFeel::panel());
            b.setColour (juce::TextButton::textColourOffId, AfterimageLookAndFeel::textPrimary());
        };
        style (importButton_);
        style (activateButton_);
        style (deactivateButton_);
        style (closeButton_);
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
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        auto panel = panelBounds();
        g.setColour (AfterimageLookAndFeel::panel());
        g.fillRoundedRectangle (panel.toFloat(), 8.0f);
        g.setColour (AfterimageLookAndFeel::glassEdge());
        g.drawRoundedRectangle (panel.toFloat(), 8.0f, 1.2f);
    }

    void resized() override
    {
        auto r = panelBounds().reduced (16);
        title_.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        status_.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);
        error_.setBounds (r.removeFromTop (36));
        r.removeFromTop (8);
        auto buttons = r.removeFromBottom (32);
        closeButton_.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (8);
        deactivateButton_.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        activateButton_.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        importButton_.setBounds (buttons);
        r.removeFromBottom (10);
        pasteBox_.setBounds (r);
    }

    void refresh()
    {
        status_.setText (manager_.getStatusMessage(), juce::dontSendNotification);
        deactivateButton_.setEnabled (manager_.isLicensed());
    }

    std::function<void()> onClose;
    std::function<void()> onChanged;

private:
    juce::Rectangle<int> panelBounds() const
    {
        auto panel = getLocalBounds().reduced (juce::jmax (24, getWidth() / 6),
                                               juce::jmax (40, getHeight() / 6));
        return panel.withSizeKeepingCentre (juce::jmin (420, panel.getWidth()),
                                            juce::jmin (360, panel.getHeight()));
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
        pasteBox_.clear();
        refresh();
        if (onChanged)
            onChanged();
    }

    afterimage::licensing::LicenseManager& manager_;
    juce::Label title_, status_, error_;
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

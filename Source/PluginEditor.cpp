/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void LookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider & slider){
    using namespace juce;
    
    auto bounds = Rectangle<float>(x, y, width, height);
    
    float currentAngle = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * sliderPosProportional;
    
    g.setColour(Colour(97u, 18u, 167u));
    g.fillEllipse(bounds);
    
    g.setColour(Colour(255u, 154u, 1u));
    g.drawEllipse(bounds, 1.f);
    
    Path p;
    
    auto centre = bounds.getCentre();
    Rectangle<float> r;
    r.setLeft(centre.getX() - 2);
    r.setRight(centre.getX() + 2);
    r.setTop(bounds.getY());
    r.setBottom(centre.getY());
    p.addRectangle(r);
    
    p.applyTransform(AffineTransform().rotated(currentAngle, centre.getX(), centre.getY()));
    
    g.fillPath(p);
}

void RotarySliderWithLabels::paint(juce::Graphics &g){
    using namespace juce;
    
    auto startAngle = degreesToRadians(180.f + 45.f);
    auto endAngle = degreesToRadians(360.f + 180.f - 45.f);
    
    auto range = getRange();
    
    auto sliderBounds = getSliderBounds();
    
    getLookAndFeel().drawRotarySlider(g,
                                      sliderBounds.getX(),
                                      sliderBounds.getY(),
                                      sliderBounds.getWidth(),
                                      sliderBounds.getHeight(),
                                      jmap(getValue(),
                                           range.getStart(),
                                           range.getEnd(),
                                           0.0,
                                           1.0),
                                      startAngle,
                                      endAngle,
                                      *this);
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const{
    auto bounds = getLocalBounds();
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    size -= 2. * getTextHeight();
    juce::Rectangle<int> r;
    r.setSize(size, size);
    r.setCentre(bounds.getCentre().getX(), bounds.getCentre().getY());
    return r;
}


ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p):
audioProcessor(p){
    const auto& params = audioProcessor.getParameters();
    for (auto param: params){
        param->addListener(this);
    }
    
    startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent(){
    const auto& params = audioProcessor.getParameters();
    for (auto param: params){
        param->removeListener(this);
    }
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue){
    parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback(){
    if(parametersChanged.compareAndSetBool(false, true)){
        // update the monochain
        ChainSettings chainSettings = getChainSettings(audioProcessor.apvts);
        auto peakCoefficients = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
        updateCoefficients(monochain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
        
        auto lowCutCoefficients = makeLowCutFilter(chainSettings, audioProcessor.getSampleRate());
        updateCutFilter(monochain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
        
        auto highCutCoefficients = makeHighCutFilter(chainSettings, audioProcessor.getSampleRate());
        updateCutFilter(monochain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
        
        //signal repaint
        repaint();
    }
}

void ResponseCurveComponent::paint (juce::Graphics& g)
{
    
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);
    
    auto responseArea = getLocalBounds();
    auto w = responseArea.getWidth();
    
    auto& lowcut = monochain.get<ChainPositions::LowCut>();
    auto& peak = monochain.get<ChainPositions::Peak>();
    auto& highcut = monochain.get<ChainPositions::HighCut>();
    
    auto sampleRate = audioProcessor.getSampleRate();
    
    std::vector<double> mags;
    mags.resize(w);
    
    for(int i = 0; i < w; i++){
        double mag = 1.f;
        auto freq = mapToLog10(double(i)/double(w), 20.0, 20000.0);
        
        if(!monochain.isBypassed<ChainPositions::Peak>()){
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        mag *= !lowcut.isBypassed<0>() ? lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        mag *= !lowcut.isBypassed<1>() ? lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        mag *= !lowcut.isBypassed<2>() ? lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        mag *= !lowcut.isBypassed<3>() ? lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        
        mag *= !highcut.isBypassed<0>() ? highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        mag *= !highcut.isBypassed<1>() ? highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        mag *= !highcut.isBypassed<2>() ? highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        mag *= !highcut.isBypassed<3>() ? highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate) : 1.0;
        
        mags[i] = Decibels::gainToDecibels(mag);
    }
    
    Path responseCurve;
    
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    const double inputMin = -24.0;
    const double inputMax = 24.0;
    auto map = [outputMin, outputMax, inputMin, inputMax](double input){
        return jmap(input, inputMin, inputMax, outputMin, outputMax);
    };
    
    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));
    
    for(int i = 0; i < w; i++){
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
    }
    
    g.setColour(Colours::orange);
    g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);
    
    g.setColour(Colours::white);
    g.strokePath(responseCurve, PathStrokeType(2.f));
}


//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p),
peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
lowCutFreqSlider(*audioProcessor.apvts.getParameter("LowCut Freq"), "Hz"),
highCutFreqSlider(*audioProcessor.apvts.getParameter("HighCut Freq"), "Hz"),
lowCutSlopeSlider(*audioProcessor.apvts.getParameter("LowCut Slope"), "dB/Oct"),
highCutSlopeSlider(*audioProcessor.apvts.getParameter("HighCut Slope"), "dB/Oct"),
responseCurveComponent(audioProcessor),
peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
lowCutFreqSliderAttachment(audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
highCutFreqSliderAttachment(audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
lowCutSlopeSliderAttachment(audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
highCutSlopeSliderAttachment(audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider)
{

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    for(auto* comp : getComps()){
        addAndMakeVisible(comp);
    }
    
    setSize(600, 400);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);

    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.33);
    auto w = responseArea.getWidth();
    
    responseCurveComponent.setBounds(responseArea);
    
//    // (Our component is opaque, so we must completely fill the background with a solid colour)
//    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
//
//    g.setColour (juce::Colours::white);
//    g.setFont (15.0f);
//    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void SimpleEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.33);
    
    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);
    
    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);
    
    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> SimpleEQAudioProcessorEditor::getComps(){
    return {
        &peakFreqSlider,
        &peakGainSlider,
        &peakQualitySlider,
        &lowCutFreqSlider,
        &highCutFreqSlider,
        &lowCutSlopeSlider,
        &highCutSlopeSlider,
        &responseCurveComponent
    };
}

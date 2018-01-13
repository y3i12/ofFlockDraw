#include "ofApp.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <math.h>

#define SGUI_CONFIG_FILE_EXT "cfg"
#define FRAMERATE            60.0f

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


#define DEBUG_DRAW
bool                        ofApp::s_debugFFt = true;
std::vector< std::string >  ofApp::s_particleBehaviors = { "Function", "Flocking", "Function & Flocking", "Follow the lead", "Optical Flow" };

#define threshold(n,t) ( std::max< float >( n - t, 0.0f ) / n )

ofApp::ofApp( std::list< std::string >& _args ) :
    m_particleEmitter( m_surface ),
    m_strobe( false ),
    m_renderOpticalFlow( nullptr )
{
    _args.pop_front();
}

//--------------------------------------------------------------
void ofApp::setup()
{
    // Initialize OFX
    ofRestoreWorkingDirectoryToDefault();// little trick to make debugging easier
    ofSetLogLevel( OF_LOG_NOTICE );
    ofSetBackgroundAuto( false );
    ofSetVerticalSync( true );
 
    ParticleEmitter::init();
    
    // Initialize GLSL
    m_post.init( ofGetWidth(), ofGetHeight() );
    m_bloomPass    = m_post.createPass< BloomPass >();
    m_noiseWrap    = m_post.createPass< NoiseWarpPass >();
    m_rgbShift     = m_post.createPass< RGBShiftPass >();
    m_zoomBlurPass = m_post.createPass< ZoomBlurPass >();
    
    m_noiseWrap->setSpeed( 0.5f );
    m_noiseWrap->setAmplitude( 0.0f );
    
    // Initialize fft processor
    m_fft.setup();
    m_fft.setVolumeRange( 50 );
    m_fft.setNormalize( true );
    
    m_audioAnalyzer.setup(
        m_fft.fft.stream.getSampleRate(),
        16384,
        m_fft.fft.stream.getNumInputChannels() );
    
    
    ofBackground( 0, 0, 0 );
    ofSetFrameRate( 60 );
    ofPoint displaySz   = ofGetWindowSize();
    
    m_video.setPixelFormat( OF_PIXELS_RGBA );
    
    
    m_frameBufferObject.allocate( displaySz.x, displaySz.y, GL_RGBA32F_ARB );
    
    // config vars
    m_cycleImageEvery = 0.0;
    
    // buffer for trails
    m_frameBufferObject.begin();
    ofClear( 0, 0, 0, 0 );
    m_frameBufferObject.end();
    
    // emitter
    m_particleEmitter.m_maxLifeTime        = 0.0;
    m_particleEmitter.m_minLifeTime        = 10.0;
    
    // GUI
    //m_gui             = new ofxDatGui( ofxDatGuiAnchor::TOP_LEFT  );
    
    //m_gui->lightColor = ci::ColorA( 1, 1, 0, 1 );
    //m_gui->textFont   = ci::Font( "Consolas", 12 );
    //m_mainPanel = m_gui->addPanel();
    
    // linking audio pointers to values
    //m_lowPointer    = &Particle::s_particleSizeRatio;  //3.0f
    //m_highPointer   = &Particle::s_particleSpeedRatio; //3.0f
    //    = &Particle::s_dampness; // 0.99f
    //m_midPointer    = &Particle::s_colorRedirection; //360.0f
    m_lowPointer  = &m_particleEmitter.m_soundLow;
    m_midPointer  = &m_particleEmitter.m_soundMid;
    m_highPointer = &m_particleEmitter.m_soundHigh;
    
    m_mainPanel = m_gui.addPanel( "Settings" );
    
    m_mainPanel->addGroup( ParticleEmitter::s_emitterParams );
    
    // m_mainPanel->addSpacer( 0, 20 );
    
    ofxGuiGroup* uiGroup = m_mainPanel->addGroup( "Function Mode" );
    
    //m_mainPanel->add< ofxGuiLabel  >( "Function Mode" );
    
    m_functionButtons.push_back( uiGroup->add< ofxGuiToggle >( "Function",               false ) );
    m_functionButtons.back()->addListener( this, &ofApp::onToggleFunction );
    
    m_functionButtons.push_back( uiGroup->add< ofxGuiToggle >( "Flocking",               false ) );
    m_functionButtons.back()->addListener( this, &ofApp::onToggleFlocking );
    
    //m_functionButtons.push_back( m_mainPanel->add< ofxGuiToggle >( "Function & Flocking",    true  ) );
    //m_functionButtons.back()->addListener( this, &ofApp::onToggleFunctionAndFlocking );
    
    m_functionButtons.push_back( uiGroup->add< ofxGuiToggle >( "Follow the lead",        false ) );
    m_functionButtons.back()->addListener( this, &ofApp::onToggleFollowTheLead );
    
    m_functionButtons.push_back( uiGroup->add< ofxGuiToggle >( "Optical Flow",           false ) );
    m_functionButtons.back()->addListener( this, &ofApp::onToggleOpticalFlow );
    
    updateFunctionType();
    
    //m_mainPanel->addSpacer( 0, 10 );
    
    //m_mainPanel->add< ofxGuiLabel  >( "FX" );
    uiGroup = m_mainPanel->addGroup( "FX" );
    uiGroup->add< ofxGuiToggle >( "RGB Shift",  m_rgbShift->getEnabled()        )->addListener( this, &ofApp::onToggleRGBShiftPass   );
    uiGroup->add< ofxGuiToggle >( "Noise Wrap", m_noiseWrap->getEnabled()       )->addListener( this, &ofApp::onToggleNoiseWarpPass  );
    uiGroup->add< ofxGuiToggle >( "Bloom Pass", m_bloomPass->getEnabled()       )->addListener( this, &ofApp::onToggleBloomPass      );
    uiGroup->add< ofxGuiToggle >( "Zoom Blur",  m_zoomBlurPass->getEnabled()    )->addListener( this, &ofApp::onToggleZoomBlurPass   );
    bool _strobe = m_strobe;
    uiGroup->add< ofxGuiToggle >( "Strobe",     _strobe                         )->addListener( this, &ofApp::onToggleStrobe         );
    uiGroup->add< ofxGuiToggle >( m_renderOpticalFlow );
    
    uiGroup = m_mainPanel->addGroup( m_particleEmitter.m_opticalFlow.parameters );
    //m_mainPanel->addSpacer( 0, 10 );
    
    m_mainPanel->add< ofxGuiLabel  >( "Audio Settings/vis" );
    m_mainPanel->add( m_smoothing );
    
    m_mainPanel->addSpacer( 0, 10 );
    
    m_openImageButton = m_mainPanel->add< ofxGuiButton >( "Open Image" );
    m_openImageButton->addListener( this, &ofApp::openImageCallBack );
    
    m_nextImageButton = m_mainPanel->add< ofxGuiButton >( "Next Image" );
    m_nextImageButton->addListener( this, &ofApp::nextImageCallBack );
    
    m_mainPanel->addSpacer( 0, 10 );
    
    m_mainPanel->addFpsPlotter();
    
    // some info
    m_mainPanel->addSpacer( 0, 10 );
    m_mainPanel->add< ofxGuiLabel  >( "Current Image:" );
    m_currentImageLabel = m_mainPanel->add< ofxGuiLabel  >( "" );
    
    // deserved credits
    m_mainPanel->addSpacer( 0, 10 );
    m_mainPanel->add< ofxGuiLabel  >( "y3i12: Yuri Ivatchkovitch" );
    m_mainPanel->add< ofxGuiLabel  >( "http://y3i12.com/"  );
    
    
    
    
    
    //m_gui->addLabel( "General Settings" );
    //m_gui->addSlider( "Pic. Cycle Time",    m_cycleImageEvery,               3.0f, 120.0f, 15.0f );
    
    //ofxDatGuiDropdown* myDropdown = new ofxDatGuiDropdown( "Behavior Mode", s_particleBehaviors );
    //myDropdown->onDropdownEvent( this, &ofApp::onDropdownEvent );
    /*ofxDatGuiDropdown* dropdown = m_gui->addDropdown( "Behavior Mode", s_particleBehaviors );
    dropdown->onDropdownEvent( this, &ofApp::onDropdownEvent );
    m_gui->addSlider( Particle::s_maxRadius             );
    m_gui->addSlider( Particle::s_particleSizeRatio     );
    m_gui->addSlider( Particle::s_particleSpeedRatio    );
    m_gui->addSlider( Particle::s_dampness              );
    m_gui->addSlider( Particle::s_colorRedirection      );
    
    m_gui->addSlider( ParticleEmitter::s_particlesPerGroup  );
    m_gui->addSlider( ParticleEmitter::s_particleGroups     );
    
    m_gui->addBreak()->setHeight( 20.0f );
    m_gui->addLabel( "Flocking Settings" );
    
    m_gui->addSlider( ParticleEmitter::s_repelStrength      );
    m_gui->addSlider( ParticleEmitter::s_alignStrength      );
    m_gui->addSlider( ParticleEmitter::s_attractStrength    );
    m_gui->addSlider( ParticleEmitter::s_zoneRadiusSqrd     );
    m_gui->addSlider( ParticleEmitter::s_lowThresh          );
    m_gui->addSlider( ParticleEmitter::s_highThresh         );
    
    m_gui->addBreak()->setHeight( 20.0f );
    m_gui->addLabel( "Audio Settings/vis" );
    m_gui->addSlider( m_smoothing );
    
    m_gui->addBreak()->setHeight( 20.0f );
    m_gui->addLabel( "FX" );
    m_gui->addToggle( "RGB Shift",  m_rgbShift->getEnabled()        )->onToggleEvent( this, &ofApp::onToggleRGBShiftPass   );
    m_gui->addToggle( "Noise Wrap", m_noiseWrap->getEnabled()       )->onToggleEvent( this, &ofApp::onToggleNoiseWarpPass  );
    m_gui->addToggle( "Bloom Pass", m_bloomPass->getEnabled()       )->onToggleEvent( this, &ofApp::onToggleBloomPass      );
    m_gui->addToggle( "Zoom Blur",  m_zoomBlurPass->getEnabled()    )->onToggleEvent( this, &ofApp::onToggleZoomBlurPass   );
    m_gui->addToggle( "Strobe",     m_strobe                        )->onToggleEvent( this, &ofApp::onToggleStrobe         );
    
    m_renderOpticalFlow     = m_gui->addToggle( "Show Optical Flow",   false );
    
    m_gui->addBreak()->setHeight( 20.0f );
    
    m_openImageButton = m_gui->addButton( "Open Image" );
    m_openImageButton->onButtonEvent( this, &ofApp::openImageCallBack );
    
    m_nextImageButton = m_gui->addButton( "Next Image" );
    m_nextImageButton->onButtonEvent( this, &ofApp::nextImageCallBack );
    
    m_gui->addBreak()->setHeight( 20.0f );
    m_gui->addFRM();
    
    // some info
    m_gui->addBreak()->setHeight( 10.0f );
    m_gui->addLabel( "Current Image:" );
    m_currentImageLabel = m_gui->addLabel( "" );
    
    // deserved credits
    m_gui->addBreak()->setHeight( 10.0f );
    m_gui->addLabel( "y3i12: Yuri Ivatchkovitch" );
    m_gui->addLabel( "http://y3i12.com/"  );*/
    
    // remove invalid paths
    for ( auto itr = m_files.begin(); itr != m_files.end(); ++itr ) {
        
        ofFile file( *itr );
        if ( !file.exists() )
        {
            auto t_itr = itr;
            
            ++t_itr;
            m_files.erase( itr );
            itr = --t_itr;
        }
    }
    
    // load images passed via args
    if ( m_files.size() > 1 )
    {
        m_cycleCounter = 0;
    }
    else
    {
        if ( m_files.size() == 0 )
        {
            m_files.push_back( "heaven.png" );
        }
        setImage( m_files.front() );
        m_cycleCounter  = -1.0;
    }
    
    // mark the time to test counters
    m_lastTime = ofGetElapsedTimef();
}

//--------------------------------------------------------------
void ofApp::update()
{
    float delta   = 0.0;
    m_currentTime = ofGetElapsedTimef();
    delta         = m_currentTime - m_lastTime;
    
    if ( !m_imageToSet.empty() )
    {
        changeImage();
    }

    // Video Update >>>
    m_video.update();
    // Video Update <<<
    
    // Audio update >>>
    m_fft.update();
    m_soundBuffer.copyFrom( m_fft.fft.getAudio(), m_fft.fft.stream.getNumInputChannels(), m_fft.fft.stream.getSampleRate() );
    m_audioAnalyzer.analyze( m_soundBuffer );
    
    m_rms               = m_audioAnalyzer.getValue( RMS,                    0, m_smoothing );
    m_power             = m_audioAnalyzer.getValue( POWER,                  0, m_smoothing );
    m_pitchFreq         = m_audioAnalyzer.getValue( PITCH_FREQ,             0, m_smoothing );
    m_pitchConf         = m_audioAnalyzer.getValue( PITCH_CONFIDENCE,       0, m_smoothing );
    m_pitchSalience     = m_audioAnalyzer.getValue( PITCH_SALIENCE,         0, m_smoothing );
    m_inharmonicity     = m_audioAnalyzer.getValue( INHARMONICITY,          0, m_smoothing );
    m_hfc               = m_audioAnalyzer.getValue( HFC,                    0, m_smoothing );
    m_specComp          = m_audioAnalyzer.getValue( SPECTRAL_COMPLEXITY,    0, m_smoothing );
    m_centroid          = m_audioAnalyzer.getValue( CENTROID,               0, m_smoothing );
    m_rollOff           = m_audioAnalyzer.getValue( ROLL_OFF,               0, m_smoothing );
    m_oddToEven         = m_audioAnalyzer.getValue( ODD_TO_EVEN,            0, m_smoothing );
    m_strongPeak        = m_audioAnalyzer.getValue( STRONG_PEAK,            0, m_smoothing );
    m_strongDecay       = m_audioAnalyzer.getValue( STRONG_DECAY,           0, m_smoothing );
    m_danceability      = m_audioAnalyzer.getValue( DANCEABILITY,           0, m_smoothing );
    //Normalized values for graphic meters:
    m_pitchFreqNorm     = m_audioAnalyzer.getValue( PITCH_FREQ,             0, m_smoothing, TRUE );
    m_hfcNorm           = m_audioAnalyzer.getValue( HFC,                    0, m_smoothing, TRUE );
    m_specCompNorm      = m_audioAnalyzer.getValue( SPECTRAL_COMPLEXITY,    0, m_smoothing, TRUE );
    m_centroidNorm      = m_audioAnalyzer.getValue( CENTROID,               0, m_smoothing, TRUE );
    m_rollOffNorm       = m_audioAnalyzer.getValue( ROLL_OFF,               0, m_smoothing, TRUE );
    m_oddToEvenNorm     = m_audioAnalyzer.getValue( ODD_TO_EVEN,            0, m_smoothing, TRUE );
    m_strongPeakNorm    = m_audioAnalyzer.getValue( STRONG_PEAK,            0, m_smoothing, TRUE );
    m_strongDecayNorm   = m_audioAnalyzer.getValue( STRONG_DECAY,           0, m_smoothing, TRUE );
    m_danceabilityNorm  = m_audioAnalyzer.getValue( DANCEABILITY,           0, m_smoothing, TRUE );
    
    m_dissonance        = m_audioAnalyzer.getValue( DISSONANCE,             0, m_smoothing );
    
    m_spectrum          = m_audioAnalyzer.getValues( SPECTRUM,              0, m_smoothing );
    m_melBands          = m_audioAnalyzer.getValues( MEL_BANDS,             0, m_smoothing );
    m_mfcc              = m_audioAnalyzer.getValues( MFCC,                  0, m_smoothing );
    m_hpcp              = m_audioAnalyzer.getValues( HPCP,                  0, m_smoothing );
    
    m_tristimulus       = m_audioAnalyzer.getValues( TRISTIMULUS,           0, m_smoothing );
    
    m_isOnset           = m_audioAnalyzer.getOnsetValue( 0 );
    
    
    auto spectrum            = m_fft.getSpectrum();
    size_t spectrumSize      = spectrum.size();
    size_t thirdSpectrumSize = spectrumSize / 3;
    
    float* valPointers[ 3 ]  = { &m_particleEmitter.m_soundLow, &m_particleEmitter.m_soundMid, &m_particleEmitter.m_soundHigh };
    *valPointers[ 0 ] = *valPointers[ 1 ] = *valPointers[ 2 ] = 0;
    for ( size_t i = 0; i < spectrumSize; i++ )
    {
        *valPointers[ std::min< size_t >( i / thirdSpectrumSize, 2 ) ] += spectrum[ i ] / thirdSpectrumSize;
    }
    // Audio update <<<
    
    // Post processing update >>>
    float rgbShift = m_particleEmitter.m_soundHigh;
    rgbShift *= rgbShift * 2;
    rgbShift /= 20.0f;
    
    m_rgbShift->setAmount( rgbShift );
    m_rgbShift->setAngle( ( sin( m_currentTime ) - 0.5f ) * m_particleEmitter.m_soundMid * PI );
    
    if ( m_spectrum.size() > 2 )
    {
        m_noiseWrap->setAmplitude( threshold( m_particleEmitter.m_soundLow, 0.3f ) / 50 );
    }
    
    m_zoomBlurPass->setCenterX( m_tristimulus[ 1 ] );
    m_zoomBlurPass->setCenterY( m_tristimulus[ 2 ] );
    m_zoomBlurPass->setDensity( m_tristimulus[ 0 ] / 25.0f );
    // Post processing update <<<

    // Image update >>>
    if ( m_cycleCounter != -1.0 )
    {
        m_cycleCounter += delta;
        
        if ( m_cycleCounter >= m_cycleImageEvery && m_files.size() > 1 )
        {
            m_cycleCounter -= m_cycleImageEvery;
            
            std::string aPath = m_files.front();
            m_files.pop_front( );
            m_files.push_back( aPath );
            
            setImage( aPath );
        }
    }
    // Image update <<<
    
    // Flow update >>>
    if ( m_video.isLoaded() )
    {
        m_surface = &m_video.getPixels();
        m_particleEmitter.updateVideo( m_video.isFrameNew(), m_video, delta );
    }
    // Flow update <<<
    
    
    // Particle update >>>
    
    Particle::s_particleSizeRatio = std::min< float >( std::max< float >( m_particleEmitter.m_soundLow * 3.0f, 0.30f ), 5.5f ) * 0.1f;
    m_particleEmitter.update( m_currentTime, delta );
    // Particle update <<<

    
    m_lastTime = m_currentTime;
}

//--------------------------------------------------------------
void ofApp::draw()
{
    ofPoint displaySz = ofGetWindowSize();
    
    // do the drawing =D
    //*
    m_frameBufferObject.begin();
    {
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        {
            ofFill();
            ofSetColor( 0, 0, 0, 1 );
            ofDrawRectangle( 0, 0, displaySz.x, displaySz.y );
        
            ofSetColor( 0, 0, 0, 255 );
            m_particleEmitter.draw();
        }
        ofDisableBlendMode();
    }
    m_frameBufferObject.end();
    
    // save what happened to the framebuffer
    ofSetColor( 255, 255, 255 );
    m_post.begin();
    m_frameBufferObject.draw( 0, 0 );
    m_post.end();
    if ( m_strobe && m_isOnset )
    {
        //blendMode = OF_BLENDMODE_ALPHA;
        //blendMode = OF_BLENDMODE_ADD;
        //blendMode = OF_BLENDMODE_MULTIPLY;
        //blendMode = OF_BLENDMODE_SUBTRACT;
        //blendMode = OF_BLENDMODE_SCREEN;
        ofEnableBlendMode( OF_BLENDMODE_SCREEN );
        {
            ofSetColor( m_tristimulus[ 0 ] * 255, m_tristimulus[ 1 ] * 255, m_tristimulus[ 2 ] * 255, 64 );
            ofDrawRectangle( 0, 0, displaySz.x, displaySz.y );
        }
        ofDisableBlendMode();
    }//*/
    
    if ( ParticleEmitter::s_debugDraw )
    {
        m_particleEmitter.debugDraw();
    }
    
    if ( 0 && s_debugFFt )
    {
        m_fft.drawHistoryGraph( ofPoint( 824,   0 ), LOW  );
        m_fft.drawHistoryGraph( ofPoint( 824, 200 ), MID  );
        m_fft.drawHistoryGraph( ofPoint( 824, 400 ), HIGH );

        ofDrawBitmapString( "LOW",  850,  20 );
        ofDrawBitmapString( "HIGH", 850, 420 );
        ofDrawBitmapString( "MID",  850, 220 );
        
        m_fft.drawBars();
        m_fft.drawDebug();
        ofDrawBitmapStringHighlight( "LOW:  " + ofToString( m_particleEmitter.m_soundLow  ), 400, 300 );
        ofDrawBitmapStringHighlight( "MID:  " + ofToString( m_particleEmitter.m_soundMid  ), 400, 320 );
        ofDrawBitmapStringHighlight( "HIGH: " + ofToString( m_particleEmitter.m_soundHigh ), 400, 340 );
    }
    
    if ( m_renderOpticalFlow )
    {
        m_particleEmitter.drawOpticalFlow();
    }
    
    if ( 0&&m_gui.getVisible() )
    {
        ofPushMatrix();
        ofTranslate(350, 0);
        int mw = 250;
        int xpos = 0;
        int ypos = 30;
        
        float value, valueNorm;
        
        ofSetColor(255);
        value = m_rms;
        string strValue = "RMS: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, value * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_power;
        strValue = "Power: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, value * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_pitchFreq;
        valueNorm = m_pitchFreqNorm;
        strValue = "Pitch Frequency: " + ofToString(value, 2) + " hz.";
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_pitchConf;
        strValue = "Pitch Confidence: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, value * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_pitchSalience;
        strValue = "Pitch Salience: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, value * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_inharmonicity;
        strValue = "Inharmonicity: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, value * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_hfc;
        valueNorm = m_hfcNorm;
        strValue = "HFC: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_specComp;
        valueNorm = m_specCompNorm;
        strValue = "Spectral Complexity: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_centroid;
        valueNorm = m_centroidNorm;
        strValue = "Centroid: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_dissonance;
        strValue = "Dissonance: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, value * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_rollOff;
        valueNorm = m_rollOffNorm;
        strValue = "Roll Off: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw , 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_oddToEven;
        valueNorm = m_oddToEvenNorm;
        strValue = "Odd To Even Harmonic Energy Ratio: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_strongPeak;
        valueNorm = m_strongPeakNorm;
        strValue = "Strong Peak: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_strongDecay;
        valueNorm = m_strongDecayNorm;
        strValue = "Strong Decay: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_isOnset;
        strValue = "Onsets: " + ofToString(value);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, value * mw, 10);
        
        ypos += 50;
        ofSetColor(255);
        value = m_danceability;
        valueNorm = m_danceabilityNorm;
        strValue = "Danceability: " + ofToString(value, 2);
        ofDrawBitmapString(strValue, xpos, ypos);
        ofSetColor(ofColor::cyan);
        ofDrawRectangle(xpos, ypos+5, valueNorm * mw, 10);
        
        
        ofPopMatrix();
        
        //-Vector Values Algorithms:
        
        ofPushMatrix();
        
        ofTranslate(700, 0);
        
        int graphH = 75;
        int yoffset = graphH + 50;
        ypos = 30;
        
        ofSetColor(255);
        ofDrawBitmapString("Spectrum: ", 0, ypos);
        ofPushMatrix();
        ofTranslate(0, ypos);
        ofSetColor(ofColor::cyan);
        float bin_w = (float) mw / m_spectrum.size();
        for (int i = 0; i < m_spectrum.size(); i++){
            float scaledValue = ofMap(m_spectrum[i], DB_MIN, DB_MAX, 0.0, 1.0, true);//clamped value
            float bin_h = -1 * (scaledValue * graphH);
            ofDrawRectangle(i*bin_w, graphH, bin_w, bin_h);
        }
        ofPopMatrix();
        
        ypos += yoffset;
        ofSetColor(255);
        ofDrawBitmapString("Mel Bands: ", 0, ypos);
        ofPushMatrix();
        ofTranslate(0, ypos);
        ofSetColor(ofColor::cyan);
        bin_w = (float) mw / m_melBands.size();
        for (int i = 0; i < m_melBands.size(); i++){
            float scaledValue = ofMap(m_melBands[i], DB_MIN, DB_MAX, 0.0, 1.0, true);//clamped value
            float bin_h = -1 * (scaledValue * graphH);
            ofDrawRectangle(i*bin_w, graphH, bin_w, bin_h);
        }
        ofPopMatrix();
        
        ypos += yoffset;
        ofSetColor(255);
        ofDrawBitmapString("MFCC: ", 0, ypos);
        ofPushMatrix();
        ofTranslate(0, ypos);
        ofSetColor(ofColor::cyan);
        bin_w = (float) mw / m_mfcc.size();
        for (int i = 0; i < m_mfcc.size(); i++){
            float scaledValue = ofMap(m_mfcc[i], 0, MFCC_MAX_ESTIMATED_VALUE, 0.0, 1.0, true);//clamped value
            float bin_h = -1 * (scaledValue * graphH);
            ofDrawRectangle(i*bin_w, graphH, bin_w, bin_h);
        }
        ofPopMatrix();
        
        ypos += yoffset;
        ofSetColor(255);
        ofDrawBitmapString("HPCP: ", 0, ypos);
        ofPushMatrix();
        ofTranslate(0, ypos);
        ofSetColor(ofColor::cyan);
        bin_w = (float) mw / m_hpcp.size();
        for (int i = 0; i < m_hpcp.size(); i++){
            //float scaledValue = ofMap(hpcp[i], DB_MIN, DB_MAX, 0.0, 1.0, true);//clamped value
            float scaledValue = m_hpcp[i];
            float bin_h = -1 * (scaledValue * graphH);
            ofDrawRectangle(i*bin_w, graphH, bin_w, bin_h);
        }
        ofPopMatrix();
        
        ypos += yoffset;
        ofSetColor(255);
        ofDrawBitmapString("Tristimulus: ", 0, ypos);
        ofPushMatrix();
        ofTranslate(0, ypos);
        ofSetColor(ofColor::cyan);
        bin_w = (float) mw / m_tristimulus.size();
        for (int i = 0; i < m_tristimulus.size(); i++){
            //float scaledValue = ofMap(hpcp[i], DB_MIN, DB_MAX, 0.0, 1.0, true);//clamped value
            float scaledValue = m_tristimulus[i];
            float bin_h = -1 * (scaledValue * graphH);
            ofDrawRectangle(i*bin_w, graphH, bin_w, bin_h);
        }
        ofPopMatrix();
        
        
        ofPopMatrix();
    }
    
    //m_fft.drawBars();
    
    // draw the UI
    //m_gui->draw();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    switch( key )
    {
#if defined DEBUG_DRAW
        case 'd':
        {
            ParticleEmitter::s_debugDraw = !ParticleEmitter::s_debugDraw;
        }
        break; //prints values of all the controls to the console
#endif
        case 'l':
        {
            std::vector< std::string > theExtensions;
            theExtensions.push_back( SGUI_CONFIG_FILE_EXT );
            
            ofFileDialogResult openFileResult= ofSystemLoadDialog( "Load settings" );
            
            //Check if the user opened a file
            if ( openFileResult.bSuccess )
            {
                ofLogVerbose("User selected a file");
                
                // We have a file, check it and process it
                std::string aPath = openFileResult.getPath();
                
                ofFile file( aPath );
                
                if ( file.exists() && file.getExtension() == SGUI_CONFIG_FILE_EXT )
                {
                    ofSystemAlertDialog( "to be implemented" );
                    // TODO
                    // m_gui->load( aPath );
                }
                else
                {
                    ofSystemAlertDialog( "Invalid file" );
                }
            }
        }
        break;
            
            
        case 's':
        {
            ofFileDialogResult saveFileResult = ofSystemSaveDialog( "*.cfg", "Save settings");
            
            if ( saveFileResult.bSuccess )
            {
                ofFile file( saveFileResult.filePath );
                
                if ( file.getExtension() != SGUI_CONFIG_FILE_EXT )
                {
                    ofSystemAlertDialog( "Invalid extension" );
                }
                else
                {
                    ofSystemAlertDialog( "to be implemented" );
                    // TODO
                    //m_gui->save( saveFileResult.filePath );
                }
            }
        }
        break;
            
        case 'h':
        {
            m_gui.getVisible() = !m_gui.getVisible();
        }
        break;
            
        case 'o':
        {
            openImage();
        }
        break;
            
        case OF_KEY_ESC:
        {
            m_particleEmitter.killAll();
            ofExit();
        }
        break;
            
        case ' ':
        {
            if ( m_files.size() == 1 )
            {
                setImage( m_files.front() );
            }
            else
            {
                nextImage();
            }
        }
            break;
    }
}

void ofApp::exit( ofEventArgs & args )
{
    m_particleEmitter.killAll();
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 
    /* Is this here?
    m_files = _event.getFiles();
    
    setImage( m_files.front(), m_currentTime );
    
    if ( m_files.size() > 1 )
    {
        m_cycleCounter = 0;
    }
     */
}

////////////////////////////////////////////////////////////////////////////////
void ofApp::onToggleFunction( bool& b )
{
    m_particleEmitter.m_updateType = b ?
        static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType |  ParticleEmitter::kFunction ) :
        static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType & ~ParticleEmitter::kFunction );
    //updateFunctionType();
    return false;
}

void ofApp::onToggleFlocking( bool& b )
{
    m_particleEmitter.m_updateType = b ?
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType |  ParticleEmitter::kFlocking ) :
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType & ~ParticleEmitter::kFlocking );
    //updateFunctionType();
    return false;
}

void ofApp::onToggleFunctionAndFlocking( bool& b )
{
    m_particleEmitter.m_updateType = b ?
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType |  ParticleEmitter::kFunctionAndFlocking ) :
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType & ~ParticleEmitter::kFunctionAndFlocking );
    //updateFunctionType();
    return false;
}

void ofApp::onToggleFollowTheLead( bool& b )
{
    m_particleEmitter.m_updateType = b ?
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType |  ParticleEmitter::kFollowTheLead ) :
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType & ~ParticleEmitter::kFollowTheLead );
    //updateFunctionType();
    return false;
}

void ofApp::onToggleOpticalFlow( bool& b )
{
    m_particleEmitter.m_updateType = b ?
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType |  ParticleEmitter::kOpticalFlow ) :
    static_cast< ParticleEmitter::UpdateType >( m_particleEmitter.m_updateType & ~ParticleEmitter::kOpticalFlow );
    //updateFunctionType();
    return false;
}

void ofApp::updateFunctionType( void )
{
    int update_type = static_cast< int >( m_particleEmitter.m_updateType );
    for ( int i = 0; i < m_functionButtons.size(); ++i )
    {
        bool value = static_cast< bool >( ( update_type >> i ) & 1 );
        ( *m_functionButtons[ i ] ) = value;
    }
}

void ofApp::onToggleStrobe( bool& b )
{
    m_strobe = !m_strobe;
    return false;
}

void ofApp::onToggleRGBShiftPass( bool& b )
{
    m_rgbShift->setEnabled( !m_rgbShift->getEnabled() );
    return false;
}

void ofApp::onToggleNoiseWarpPass( bool& b )
{
    m_noiseWrap->setEnabled( !m_noiseWrap->getEnabled() );
    return false;
}

void ofApp::onToggleBloomPass( bool& b )
{
    m_bloomPass->setEnabled( !m_bloomPass->getEnabled() );
    return false;
}

void ofApp::onToggleZoomBlurPass( bool& b )
{
    m_zoomBlurPass->setEnabled( !m_bloomPass->getEnabled() );
    return false;
}

void ofApp::openImageCallBack( void )
{
    openImage();
}

bool ofApp::openImage( void )
{
    m_files.clear();
    /*
     Use http://openframeworks.cc/documentation/utils/ofDirectory/ to scan dirs
     BMP files [reading, writing]
     Dr. Halo CUT files [reading] *
     DDS files [reading]
     EXR files [reading, writing]
     Raw Fax G3 files [reading]
     GIF files [reading, writing]
     HDR files [reading, writing]
     ICO files [reading, writing]
     IFF files [reading]
     JBIG files [reading, writing] **
     JNG files [reading, writing]
     JPEG/JIF files [reading, writing]
     JPEG-2000 File Format [reading, writing]
     JPEG-2000 codestream [reading, writing]
     JPEG-XR files [reading, writing]
     KOALA files [reading]
     Kodak PhotoCD files [reading]
     MNG files [reading]
     PCX files [reading]
     PBM/PGM/PPM files [reading, writing]
     PFM files [reading, writing]
     PNG files [reading, writing]
     Macintosh PICT files [reading]
     Photoshop PSD files [reading]
     RAW camera files [reading]
     Sun RAS files [reading]
     SGI files [reading]
     TARGA files [reading, writing]
     TIFF files [reading, writing]
     WBMP files [reading, writing]
     WebP files [reading, writing]
     XBM files [reading]
     XPM files [reading, writing]
     */
    ofFileDialogResult fileOpenResult = ofSystemLoadDialog( "Load an image" );
    
    if ( fileOpenResult.bSuccess )
    {
        ofFile file(fileOpenResult.getPath() );
        if ( file.exists() )
        {
            setImage( file.getAbsolutePath() );
        }
        
        m_cycleCounter  = -1.0;
    }
    
    return false;
}

////////////////////////////////////////////////////////////////////////////////

void ofApp::nextImageCallBack( void )
{
    nextImage();
}

bool ofApp::nextImage( void )
{
    m_cycleCounter = m_cycleImageEvery;
    return false;
}

////////////////////////////////////////////////////////////////////////////////

void ofApp::updateOutputArea( ofVec2f _imageSize )
{
    ofPoint   windowSz              = ofGetWindowSize();
    m_particleEmitter.m_sizeFactor  = fmin( windowSz.x / _imageSize.x, windowSz.y / _imageSize.y );
    
    m_particleEmitter.setInputArea( _imageSize );
    
    _imageSize *= m_particleEmitter.m_sizeFactor;
    m_outputArea.x          = windowSz.x / 2 - _imageSize.x / 2;
    m_outputArea.y          = windowSz.y / 2 - _imageSize.y / 2;
    m_outputArea.width      = _imageSize.x;
    m_outputArea.height     = _imageSize.y;
    
    m_particleEmitter.m_position = ofVec2f( static_cast< float >( m_outputArea.x ), static_cast< float >( m_outputArea.y ) );
}

void ofApp::setImage( std::string _path )
{
    m_imageToSet = _path;
}

void ofApp::changeImage( void )
{
    m_particleEmitter.pauseThreads();
    m_particleEmitter.waitThreadedUpdate();
    
    if ( m_video.isLoaded() )
    {
        m_video.close();
    }
    
    // load the image, resize and set the texture
    ofVec2f   aSize;
    if ( m_texture.load( m_imageToSet ) )
    {
        aSize.x         = m_texture.getWidth();
        aSize.y         = m_texture.getHeight();
        m_surface       = &m_texture.getPixels();
    }
    else if ( m_video.load( m_imageToSet ) )
    {
        aSize.x         = m_video.getWidth();
        aSize.y         = m_video.getHeight();
        m_surface       = &m_video.getPixels();
        m_video.setLoopState( OF_LOOP_NORMAL );
        m_video.play();
    }
    else
    {
        return;
    }
    
    
    // update  the image name
    m_currentImageLabel->setName( m_imageToSet );
    
    // update the output area
    //ofVec2f newSize( aSize.x * m_particleEmitter.m_sizeFactor, aSize.y * m_particleEmitter.m_sizeFactor );
    updateOutputArea( aSize );
    
    // resets the cycle counter;
    m_cycleCounter = 0.0;
    
    // set the filename in the widget
    ofFile file( m_imageToSet );
    m_currentImageLabel->setName( file.getFileName() );
    
    m_particleEmitter.continueThreads();
    
    m_imageToSet.clear();
}

////////////////////////////////////////////////////////////////////////////////

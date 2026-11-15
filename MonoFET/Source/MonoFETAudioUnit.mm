#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#include <atomic>
#include "MonoFETProcessor.hpp"
#include "MonoFETPresets.hpp"

enum { kInput = 0, kOutput, kAttack, kRelease, kRatio, kThreshold, kLookahead, kLookOn, kHpf, kLpf, kFilterMode, kCompOn, kMix, kPeqFreq, kPeqGain, kPeqQ, kNumParams };

@interface MonoFETAudioUnit : AUAudioUnit
@end

@implementation MonoFETAudioUnit {
    AUAudioUnitBus *_inBus, *_outBus;
    AUAudioUnitBusArray *_inBusses, *_outBusses;
    AUParameterTree *_tree;
    NSArray<AUAudioUnitPreset *> *_presets;
    AUAudioUnitPreset *_currentPreset;
    MonoFETProcessor _dsp;
    std::atomic<float> _p[kNumParams];
}

- (instancetype)initWithComponentDescription:(AudioComponentDescription)desc
                                     options:(AudioComponentInstantiationOptions)options
                                       error:(NSError **)outError {
    self = [super initWithComponentDescription:desc options:options error:outError];
    if (!self) return nil;

    AVAudioFormat *inFmt  = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:1];
    AVAudioFormat *outFmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:1];
    _inBus  = [[AUAudioUnitBus alloc] initWithFormat:inFmt  error:outError];
    _outBus = [[AUAudioUnitBus alloc] initWithFormat:outFmt error:outError];
    if (!_inBus || !_outBus) return nil;
    _inBus.maximumChannelCount  = 1;
    _outBus.maximumChannelCount = 1;
    _inBusses  = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeInput  busses:@[_inBus]];
    _outBusses = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeOutput busses:@[_outBus]];

    AudioUnitParameterOptions fl = kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable;
    AUParameter *pIn  = [AUParameterTree createParameterWithIdentifier:@"input"   name:@"Input"   address:kInput   min:0    max:48   unit:kAudioUnitParameterUnit_Decibels      unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pOut = [AUParameterTree createParameterWithIdentifier:@"output"  name:@"Output"  address:kOutput  min:-24  max:24   unit:kAudioUnitParameterUnit_Decibels      unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pAtk = [AUParameterTree createParameterWithIdentifier:@"attack"  name:@"Attack"  address:kAttack  min:20   max:800  unit:kAudioUnitParameterUnit_Generic       unitName:@"us" flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pRel = [AUParameterTree createParameterWithIdentifier:@"release" name:@"Release" address:kRelease min:50   max:1100 unit:kAudioUnitParameterUnit_Milliseconds  unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pRat = [AUParameterTree createParameterWithIdentifier:@"ratio"   name:@"Ratio"   address:kRatio   min:1.5  max:20   unit:kAudioUnitParameterUnit_Ratio         unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pThr = [AUParameterTree createParameterWithIdentifier:@"threshold" name:@"Threshold" address:kThreshold min:-60 max:0 unit:kAudioUnitParameterUnit_Decibels unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pLook = [AUParameterTree createParameterWithIdentifier:@"lookahead" name:@"Lookahead" address:kLookahead min:0 max:5 unit:kAudioUnitParameterUnit_Milliseconds unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pOn = [AUParameterTree createParameterWithIdentifier:@"lookon" name:@"Lookahead On" address:kLookOn min:0 max:1 unit:kAudioUnitParameterUnit_Boolean unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pHp = [AUParameterTree createParameterWithIdentifier:@"hpf" name:@"HPF" address:kHpf min:20 max:300 unit:kAudioUnitParameterUnit_Hertz unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pLp = [AUParameterTree createParameterWithIdentifier:@"lpf" name:@"LPF" address:kLpf min:300 max:20000 unit:kAudioUnitParameterUnit_Hertz unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pMode = [AUParameterTree createParameterWithIdentifier:@"filtermode" name:@"Filter Mode" address:kFilterMode min:0 max:1 unit:kAudioUnitParameterUnit_Indexed unitName:nil flags:fl valueStrings:@[@"Sidechain", @"EQ"] dependentParameters:nil];
    AUParameter *pComp = [AUParameterTree createParameterWithIdentifier:@"compon" name:@"Compressor On" address:kCompOn min:0 max:1 unit:kAudioUnitParameterUnit_Boolean unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pMix  = [AUParameterTree createParameterWithIdentifier:@"mix" name:@"Mix" address:kMix min:0 max:100 unit:kAudioUnitParameterUnit_Percent unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pPf = [AUParameterTree createParameterWithIdentifier:@"peqfreq" name:@"PEQ Freq" address:kPeqFreq min:100 max:10000 unit:kAudioUnitParameterUnit_Hertz unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pPg = [AUParameterTree createParameterWithIdentifier:@"peqgain" name:@"PEQ Gain" address:kPeqGain min:-18 max:18 unit:kAudioUnitParameterUnit_Decibels unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pPq = [AUParameterTree createParameterWithIdentifier:@"peqq" name:@"PEQ Q" address:kPeqQ min:0.3 max:10 unit:kAudioUnitParameterUnit_Generic unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    pIn.value = 12; pOut.value = 0; pAtk.value = 200; pRel.value = 400; pRat.value = 4; pThr.value = -18; pLook.value = 2; pOn.value = 1; pHp.value = 20; pLp.value = 20000; pMode.value = 0; pComp.value = 1; pMix.value = 100; pPf.value = 1000; pPg.value = 0; pPq.value = 1;
    for (AUParameter *prm in @[pIn, pOut, pAtk, pRel, pRat, pThr, pLook, pOn, pHp, pLp, pMode, pComp, pMix, pPf, pPg, pPq]) _p[prm.address].store(prm.value);

    _tree = [AUParameterTree createTreeWithChildren:@[pIn, pOut, pAtk, pRel, pRat, pThr, pLook, pOn, pHp, pLp, pMode, pComp, pMix, pPf, pPg, pPq]];
    std::atomic<float> *p = _p;
    __weak MonoFETAudioUnit *weakSelf = self;
    _tree.implementorValueObserver = ^(AUParameter *param, AUValue value) {
        p[param.address].store(value);
        if (param.address == kLookOn) {   // a latencia muda: avisa o host
            MonoFETAudioUnit *s = weakSelf;
            [s willChangeValueForKey:@"latency"];
            [s didChangeValueForKey:@"latency"];
        }
    };
    _tree.implementorValueProvider = ^AUValue(AUParameter *param) { return p[param.address].load(); };

    NSMutableArray *list = [NSMutableArray array];
    for (int i = 0; i < kMonoFETNumPresets; ++i) {
        AUAudioUnitPreset *pr = [AUAudioUnitPreset new];
        pr.number = i;
        pr.name = [NSString stringWithUTF8String:kMonoFETPresets[i].name];
        [list addObject:pr];
    }
    _presets = list;
    return self;
}

- (NSTimeInterval)latency { return _p[kLookOn].load() >= 0.5f ? MonoFETProcessor::kLatencySec : 0.0; }
- (BOOL)supportsUserPresets { return YES; }
- (NSArray<AUAudioUnitPreset *> *)factoryPresets { return _presets; }
- (AUAudioUnitPreset *)currentPreset { return _currentPreset; }
- (void)setCurrentPreset:(AUAudioUnitPreset *)preset {
    if (preset.number >= 0 && preset.number < kMonoFETNumPresets) {
        const MonoFETPreset &q = kMonoFETPresets[preset.number];
        const float v[kNumParams] = {q.inDb, q.outDb, q.attackUs, q.releaseMs, q.ratio, q.thrDb, q.lookMs, q.lookOn, q.hpfHz, q.lpfHz, q.eqMode, q.compOn, q.mixPct, q.peqHz, q.peqDb, q.peqQ};
        for (int a = 0; a < kNumParams; ++a) [_tree parameterWithAddress:a].value = v[a];
    }
    _currentPreset = preset;   // numero negativo = preset de usuario (parametros vem do estado salvo)
}

- (AUAudioUnitBusArray *)inputBusses  { return _inBusses; }
- (AUAudioUnitBusArray *)outputBusses { return _outBusses; }
- (AUParameterTree *)parameterTree    { return _tree; }
- (NSArray<NSNumber *> *)channelCapabilities { return @[@1, @1]; }   // 1 entrada, 1 saida

- (BOOL)shouldChangeToFormat:(AVAudioFormat *)format forBus:(AUAudioUnitBus *)bus {
    return format.channelCount == 1u;
}

- (BOOL)allocateRenderResourcesAndReturnError:(NSError **)outError {
    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;
    _dsp.prepare(_outBus.format.sampleRate);
    return YES;
}

- (AUInternalRenderBlock)internalRenderBlock {
    MonoFETProcessor *dsp = &_dsp;
    std::atomic<float> *p = _p;

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                              AUAudioFrameCount frames, NSInteger outputBusNumber,
                              AudioBufferList *out, const AURenderEvent *events,
                              AURenderPullInputBlock pull) {
        if (!pull || out->mNumberBuffers < 1) return kAudioUnitErr_Uninitialized;

        AudioBufferList in;
        in.mNumberBuffers = 1;
        in.mBuffers[0].mNumberChannels = 1;
        in.mBuffers[0].mDataByteSize = frames * sizeof(float);
        in.mBuffers[0].mData = nullptr;
        AudioUnitRenderActionFlags f = 0;
        AUAudioUnitStatus st = pull(&f, ts, frames, 0, &in);
        if (st != noErr) return st;

        float *o = (float *)out->mBuffers[0].mData;
        if (!o) { o = (float *)in.mBuffers[0].mData; out->mBuffers[0].mData = o; }
        out->mBuffers[0].mDataByteSize = frames * sizeof(float);

        dsp->setParams(p[kInput].load(), p[kOutput].load(), p[kAttack].load(), p[kRelease].load(),
                       p[kRatio].load(), p[kThreshold].load(), p[kLookahead].load(), p[kLookOn].load() >= 0.5f,
                       p[kHpf].load(), p[kLpf].load(), p[kFilterMode].load() >= 0.5f, p[kCompOn].load() >= 0.5f,
                       p[kMix].load() * 0.01f, p[kPeqFreq].load(), p[kPeqGain].load(), p[kPeqQ].load());
        dsp->process((const float *)in.mBuffers[0].mData, o, (int)frames);
        return noErr;
    };
}
@end

@interface MonoFETFactory : NSObject <AUAudioUnitFactory>
@end

@implementation MonoFETFactory
- (AUAudioUnit *)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc error:(NSError **)error {
    return [[MonoFETAudioUnit alloc] initWithComponentDescription:desc options:0 error:error];
}
- (void)beginRequestWithExtensionContext:(NSExtensionContext *)context {}
@end

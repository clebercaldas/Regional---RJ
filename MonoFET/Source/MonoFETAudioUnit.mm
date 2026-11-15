#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#include <atomic>
#include <vector>
#include "MonoFETProcessor.hpp"

enum { kInput = 0, kOutput, kAttack, kRelease, kRatio, kThreshold, kNumParams };

@interface MonoFETAudioUnit : AUAudioUnit
@end

@implementation MonoFETAudioUnit {
    AUAudioUnitBus *_inBus, *_outBus;
    AUAudioUnitBusArray *_inBusses, *_outBusses;
    AUParameterTree *_tree;
    MonoFETProcessor _dsp;
    std::atomic<float> _p[kNumParams];
    std::vector<float> _scratch;
}

- (instancetype)initWithComponentDescription:(AudioComponentDescription)desc
                                     options:(AudioComponentInstantiationOptions)options
                                       error:(NSError **)outError {
    self = [super initWithComponentDescription:desc options:options error:outError];
    if (!self) return nil;

    AVAudioFormat *inFmt  = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:1];
    AVAudioFormat *outFmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:2];
    _inBus  = [[AUAudioUnitBus alloc] initWithFormat:inFmt  error:outError];
    _outBus = [[AUAudioUnitBus alloc] initWithFormat:outFmt error:outError];
    if (!_inBus || !_outBus) return nil;
    _inBus.maximumChannelCount  = 1;
    _outBus.maximumChannelCount = 2;
    _inBusses  = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeInput  busses:@[_inBus]];
    _outBusses = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeOutput busses:@[_outBus]];

    AudioUnitParameterOptions fl = kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable;
    AUParameter *pIn  = [AUParameterTree createParameterWithIdentifier:@"input"   name:@"Input"   address:kInput   min:0    max:48   unit:kAudioUnitParameterUnit_Decibels      unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pOut = [AUParameterTree createParameterWithIdentifier:@"output"  name:@"Output"  address:kOutput  min:-24  max:24   unit:kAudioUnitParameterUnit_Decibels      unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pAtk = [AUParameterTree createParameterWithIdentifier:@"attack"  name:@"Attack"  address:kAttack  min:20   max:800  unit:kAudioUnitParameterUnit_Generic       unitName:@"us" flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pRel = [AUParameterTree createParameterWithIdentifier:@"release" name:@"Release" address:kRelease min:50   max:1100 unit:kAudioUnitParameterUnit_Milliseconds  unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pRat = [AUParameterTree createParameterWithIdentifier:@"ratio"   name:@"Ratio"   address:kRatio   min:1.5  max:20   unit:kAudioUnitParameterUnit_Ratio         unitName:nil  flags:fl valueStrings:nil dependentParameters:nil];
    AUParameter *pThr = [AUParameterTree createParameterWithIdentifier:@"threshold" name:@"Threshold" address:kThreshold min:-60 max:0 unit:kAudioUnitParameterUnit_Decibels unitName:nil flags:fl valueStrings:nil dependentParameters:nil];
    pIn.value = 12; pOut.value = 0; pAtk.value = 200; pRel.value = 400; pRat.value = 4; pThr.value = -18;
    for (AUParameter *prm in @[pIn, pOut, pAtk, pRel, pRat, pThr]) _p[prm.address].store(prm.value);

    _tree = [AUParameterTree createTreeWithChildren:@[pIn, pOut, pAtk, pRel, pRat, pThr]];
    std::atomic<float> *p = _p;
    _tree.implementorValueObserver = ^(AUParameter *param, AUValue value) { p[param.address].store(value); };
    _tree.implementorValueProvider = ^AUValue(AUParameter *param) { return p[param.address].load(); };
    return self;
}

- (AUAudioUnitBusArray *)inputBusses  { return _inBusses; }
- (AUAudioUnitBusArray *)outputBusses { return _outBusses; }
- (AUParameterTree *)parameterTree    { return _tree; }
- (NSArray<NSNumber *> *)channelCapabilities { return @[@1, @2]; }   // 1 entrada, 2 saidas

- (BOOL)shouldChangeToFormat:(AVAudioFormat *)format forBus:(AUAudioUnitBus *)bus {
    return format.channelCount == (bus == _inBus ? 1u : 2u);
}

- (BOOL)allocateRenderResourcesAndReturnError:(NSError **)outError {
    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;
    _dsp.prepare(_outBus.format.sampleRate);
    _scratch.assign(self.maximumFramesToRender, 0.f);
    return YES;
}

- (AUInternalRenderBlock)internalRenderBlock {
    MonoFETProcessor *dsp = &_dsp;
    std::atomic<float> *p = _p;
    std::vector<float> *scratch = &_scratch;

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                              AUAudioFrameCount frames, NSInteger outputBusNumber,
                              AudioBufferList *out, const AURenderEvent *events,
                              AURenderPullInputBlock pull) {
        if (!pull || out->mNumberBuffers < 2 || frames > scratch->size()) return kAudioUnitErr_Uninitialized;

        AudioBufferList in;
        in.mNumberBuffers = 1;
        in.mBuffers[0].mNumberChannels = 1;
        in.mBuffers[0].mDataByteSize = frames * sizeof(float);
        in.mBuffers[0].mData = nullptr;
        AudioUnitRenderActionFlags f = 0;
        AUAudioUnitStatus st = pull(&f, ts, frames, 0, &in);
        if (st != noErr) return st;

        float *l = (float *)out->mBuffers[0].mData;
        float *r = (float *)out->mBuffers[1].mData;
        if (!l) { l = (float *)in.mBuffers[0].mData; out->mBuffers[0].mData = l; }
        if (!r) { r = scratch->data();               out->mBuffers[1].mData = r; }
        out->mBuffers[0].mDataByteSize = out->mBuffers[1].mDataByteSize = frames * sizeof(float);

        dsp->setParams(p[kInput].load(), p[kOutput].load(), p[kAttack].load(), p[kRelease].load(),
                       p[kRatio].load(), p[kThreshold].load());
        dsp->process((const float *)in.mBuffers[0].mData, l, r, (int)frames);
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

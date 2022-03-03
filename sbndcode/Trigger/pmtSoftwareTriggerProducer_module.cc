////////////////////////////////////////////////////////////////////////
// Class:       pmtSoftwareTriggerProducer
// Plugin Type: producer (Unknown Unknown)
// File:        pmtSoftwareTriggerProducer_module.cc
//
// Generated at Thu Feb 17 13:22:51 2022 by Patrick Green using cetskelgen
// from  version .
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art_root_io/TFileService.h"

#include "sbndaq-artdaq-core/Overlays/Common/CAENV1730Fragment.hh"
#include "sbndaq-artdaq-core/Overlays/FragmentType.hh"
#include "artdaq-core/Data/Fragment.hh"
#include "artdaq-core/Data/ContainerFragment.hh"

#include "sbnobj/SBND/Trigger/pmtSoftwareTrigger.hh"

// ROOT includes
#include "TH1D.h"
#include "TFile.h"
#include "TTree.h"

#include <memory>
#include <algorithm>
#include <valarray>
#include <numeric>

namespace sbnd {
  namespace trigger {
    class pmtSoftwareTriggerProducer;
  }
}

class sbnd::trigger::pmtSoftwareTriggerProducer : public art::EDProducer {
public:
  explicit pmtSoftwareTriggerProducer(fhicl::ParameterSet const& p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  pmtSoftwareTriggerProducer(pmtSoftwareTriggerProducer const&) = delete;
  pmtSoftwareTriggerProducer(pmtSoftwareTriggerProducer&&) = delete;
  pmtSoftwareTriggerProducer& operator=(pmtSoftwareTriggerProducer const&) = delete;
  pmtSoftwareTriggerProducer& operator=(pmtSoftwareTriggerProducer&&) = delete;

  // Required functions.
  void produce(art::Event& e) override;

private:

  // Declare member data here.

  // fhicl parameters
  art::Persistable is_persistable_;
  double fTriggerTimeOffset;    // offset of trigger time, default 0.5 sec
  double fBeamWindowLength; // beam window length after trigger time, default 1.6us
  uint32_t fWvfmLength;
  bool fVerbose;
  bool fSaveHists;

  std::string fBaselineAlgo;
  double fInputBaseline;

  bool fAreaToPE; // Use area to calculate number of PEs
  double fSPEArea; // If AreaToPE is true, this number is used as single PE area (in ADC counts)

  // event information
  int fRun, fSubrun;
  art::EventNumber_t fEvent;

  // beam window
  // set in artdaqFragment producer, in reality would be provided by event builder
  bool foundBeamTrigger;
  uint32_t beamWindowStart;
  uint32_t beamWindowEnd;

  // waveforms
  uint32_t fTriggerTime;
  bool fWvfmsFound;
  std::vector<std::vector<uint16_t>> fWvfmsVec;
  // std::vector<double> fWvfmsBaseline;
  // std::vector<double> fWvfmsBaselineSigma; 

  // pmt information 
  std::vector<sbnd::trigger::pmtInfo> fpmtInfoVec;

  std::stringstream histname; //raw waveform hist name
  art::ServiceHandle<art::TFileService> tfs;

  void checkCAEN1730FragmentTimeStamp(const artdaq::Fragment &frag);
  void analyzeCAEN1730Fragment(const artdaq::Fragment &frag);
  void calculateBaseline(int i_ch);
  void SimpleThreshAlgo(int i_ch);

};


sbnd::trigger::pmtSoftwareTriggerProducer::pmtSoftwareTriggerProducer(fhicl::ParameterSet const& p)
  : EDProducer{p},
  is_persistable_(p.get<bool>("is_persistable", true) ? art::Persistable::Yes : art::Persistable::No),
  fTriggerTimeOffset(p.get<double>("TriggerTimeOffset", 0.5)),
  fBeamWindowLength(p.get<double>("BeamWindowLength", 1.6)), 
  fWvfmLength(p.get<uint32_t>("WvfmLength", 5120)),
  fVerbose(p.get<bool>("Verbose", false)),
  fSaveHists(p.get<bool>("SaveHists",false)),
  fBaselineAlgo(p.get<std::string>("BaselineAlgo", "estimate")),
  fInputBaseline(p.get<double>("InputBaseline", 8000)),
  fAreaToPE(p.get<bool>("AreaToPE", false)),
  fSPEArea(p.get<double>("SPEArea", 66.33))
  // More initializers here.
{
  // Call appropriate produces<>() functions here.
  produces< sbnd::trigger::pmtSoftwareTrigger >("", is_persistable_);

  beamWindowStart = fTriggerTimeOffset*1e9;
  beamWindowEnd = beamWindowStart + fBeamWindowLength*1000;
}

void sbnd::trigger::pmtSoftwareTriggerProducer::produce(art::Event& e)
{
  // Implementation of required member function here.

  // event information
  fRun = e.run();
  fSubrun = e.subRun();
  fEvent = e.id().event();

  if (fVerbose) std::cout << "Processing Run: " << fRun << ", Subrun: " << fSubrun << ", Event: " << fEvent << std::endl;

  // reset for this event
  foundBeamTrigger = false;
  fWvfmsFound = false;
  fWvfmsVec.clear(); fWvfmsVec.resize(15*8); // 15 pmt channels per fragment, 8 fragments per trigger
  fpmtInfoVec.clear(); fpmtInfoVec.resize(15*8); 


  // get fragment handles
  std::vector<art::Handle<artdaq::Fragments>> fragmentHandles = e.getMany<std::vector<artdaq::Fragment>>();

  // loop over fragment handles
  for (auto &handle : fragmentHandles) {
    if (!handle.isValid() || handle->size() == 0) continue;
    if (handle->front().type()==sbndaq::detail::FragmentType::CAENV1730) {
      if (fVerbose)   std::cout << "Found " << handle->size() << " CAEN1730 fragments" << std::endl;
      
      // identify whether any fragments correspond to the beam spill
      // loop over fragments, in steps of 8
      size_t beamFragmentIdx = 9999;
      for (size_t fragmentIdx = 0; fragmentIdx < handle->size(); fragmentIdx += 8) {
        checkCAEN1730FragmentTimeStamp(handle->at(fragmentIdx));
        if (foundBeamTrigger) {
          beamFragmentIdx = fragmentIdx;
          if (fVerbose) std::cout << "Found fragment in time with beam at index: " << beamFragmentIdx << std::endl;
          break;
        }
      }
      // if set of fragment in time with beam found, process waveforms
      if (foundBeamTrigger && beamFragmentIdx != 9999) {
        for (size_t fragmentIdx = beamFragmentIdx; fragmentIdx < beamFragmentIdx+8; fragmentIdx++) {
          analyzeCAEN1730Fragment(handle->at(fragmentIdx));
        }
        fWvfmsFound = true;
      }
    }
  } // end loop over handles

  // object to store trigger metrics in
  std::unique_ptr<sbnd::trigger::pmtSoftwareTrigger> pmtSoftwareTriggerMetrics = std::make_unique<sbnd::trigger::pmtSoftwareTrigger>();

  if (foundBeamTrigger && fWvfmsFound) {

    pmtSoftwareTriggerMetrics->foundBeamTrigger = true;
    // store timestamp of trigger, relative to beam window start
    pmtSoftwareTriggerMetrics->triggerTimestamp = fTriggerTime - beamWindowStart;
    if (fVerbose) std::cout << "Saving trigger timestamp: " << fTriggerTime - beamWindowStart << " ns" << std::endl;
  
    // wvfm loop to calculate metrics 
    for (int i_ch = 0; i_ch < 120; ++i_ch){
      auto pmtInfo = fpmtInfoVec.at(i_ch);
      // calculate baseline 
      if (fBaselineAlgo == "constant"){ pmtInfo.baseline=fInputBaseline; pmtInfo.baselineSigma = 2; }
      else if (fBaselineAlgo == "estimate") calculateBaseline(i_ch);

      SimpleThreshAlgo(i_ch); 
    }
    // start histo 
    if (fSaveHists == true){
      int hist_id = -1; 
      for (size_t i_wvfm = 0; i_wvfm < fWvfmsVec.size(); ++i_wvfm){
        std::vector<uint16_t> wvfm = fWvfmsVec[i_wvfm];
        hist_id++;
        if (fEvent<3){
            histname.str(std::string());
            histname << "event_" << fEvent
                    << "_channel_" << i_wvfm
                    << "_" << hist_id;
            //Create a new histogram for binary waveform
            double StartTime = ((fTriggerTime-beamWindowStart) - 500)*1e-3; // us
            double EndTime = StartTime + (5210*2)*1e-03; // us 
            TH1D *wvfmHist = tfs->make< TH1D >(histname.str().c_str(), "Raw Waveform", wvfm.size(), StartTime, EndTime);
            wvfmHist->GetXaxis()->SetTitle("t (#mus)");
            for(unsigned int i = 0; i < wvfm.size(); i++) {
              wvfmHist->SetBinContent(i + 1, (double)wvfm[i]);
            }
        } 
      } // end histo
    }
  }
  else{
    pmtSoftwareTriggerMetrics->foundBeamTrigger = false;
    if (fVerbose) std::cout << "Beam and wvfms not found" << std::endl;
  }
  e.put(std::move(pmtSoftwareTriggerMetrics));      

}

void sbnd::trigger::pmtSoftwareTriggerProducer::checkCAEN1730FragmentTimeStamp(const artdaq::Fragment &frag) {

  // get fragment metadata
  sbndaq::CAENV1730Fragment bb(frag);
  auto const* md = bb.Metadata();

  // access timestamp
  uint32_t timestamp = md->timeStampNSec;

  // access beam signal, in ch15 of first PMT of each fragment set
  // check entry 500 (0us), at trigger time
  const uint16_t* data_begin = reinterpret_cast<const uint16_t*>(frag.dataBeginBytes() 
                 + sizeof(sbndaq::CAENV1730EventHeader));
  const uint16_t* value_ptr =  data_begin;
  uint16_t value = 0;

  size_t ch_offset = (size_t)(15*fWvfmLength);
  size_t tr_offset = fTriggerTimeOffset*1e3;

  value_ptr = data_begin + ch_offset + tr_offset; // pointer arithmetic 
  value = *(value_ptr);
  
  if (value == 1 && timestamp >= beamWindowStart && timestamp <= beamWindowEnd) {
    foundBeamTrigger = true;
    fTriggerTime = timestamp;
  }
}

void sbnd::trigger::pmtSoftwareTriggerProducer::analyzeCAEN1730Fragment(const artdaq::Fragment &frag) {
  
  // access fragment ID; index of fragment out of set of 8 fragments
  int fragId = static_cast<int>(frag.fragmentID()); 

  // access waveforms in fragment and save
  const uint16_t* data_begin = reinterpret_cast<const uint16_t*>(frag.dataBeginBytes() 
                 + sizeof(sbndaq::CAENV1730EventHeader));
  const uint16_t* value_ptr =  data_begin;
  uint16_t value = 0;

  // channel offset
  size_t nChannels = 15; // 15 pmts per fragment
  size_t ch_offset = 0;

  // loop over channels
  for (size_t i_ch = 0; i_ch < nChannels; ++i_ch){
    fWvfmsVec[i_ch + nChannels*fragId].resize(fWvfmLength);
    ch_offset = (size_t)(i_ch * fWvfmLength);
    //--loop over waveform samples
    for(size_t i_t = 0; i_t < fWvfmLength; ++i_t){ 
      value_ptr = data_begin + ch_offset + i_t; // pointer arithmetic
      value = *(value_ptr);
      fWvfmsVec[i_ch + nChannels*fragId][i_t] = value;
    } //--end loop samples
  } //--end loop channels
}

void sbnd::trigger::pmtSoftwareTriggerProducer::calculateBaseline(int i_ch){
  auto wvfm = fWvfmsVec[i_ch];
  auto &pmtInfo = fpmtInfoVec[i_ch]; 
  // assuming that the first 500 ns doesn't include peaks, find the mean of the ADC count as the baseline 
  // this is also assuming the sampling rate of the waveform is 1 ns 
  std::vector<uint16_t> subset = std::vector<uint16_t>(wvfm.begin(), wvfm.begin()+500);
  double subset_mean = (std::accumulate(subset.begin(), subset.end(), 0))/(subset.size()); 
  double val = 0;
  for (size_t i=0; i<subset.size();i++){ val += (subset[i] - subset_mean)*(subset[i] - subset_mean);}
  double subset_stddev = sqrt(val/subset.size()); 

  // if the first 500 ns seem to be messy, use the last 500 
  if (subset_stddev > 3){ // make this fcl parameter? 
    val = 0; subset.clear(); subset_stddev = 0;
    subset = std::vector<uint16_t>(wvfm.end()-500, wvfm.end());
    subset_mean = (std::accumulate(subset.begin(), subset.end(), 0))/(subset.size());
    for (size_t i=0; i<subset.size();i++){ val += (subset[i] - subset_mean)*(subset[i] - subset_mean);}
    subset_stddev = sqrt(val/subset.size()); 
  }
  pmtInfo.baseline = subset_mean;
  pmtInfo.baselineSigma = subset_stddev;
}

void sbnd::trigger::pmtSoftwareTriggerProducer::SimpleThreshAlgo(int i_ch){
  auto wvfm = fWvfmsVec[i_ch];
  auto &pmtInfo = fpmtInfoVec[i_ch]; 
  double baseline = pmtInfo.baseline;
  double baseline_sigma = pmtInfo.baselineSigma;
  
  bool fire = false; // bool for if pulse has been detected
  int counter = 0; // counts the bin of the waveform

  // these should be fcl parameters 
  double start_adc_thres = 5, end_adc_thres = 2; 
  double nsigma_start = 5, nsigma_end = 3; 
  
  auto start_threshold = ( start_adc_thres > (nsigma_start * baseline_sigma) ? (baseline-start_adc_thres) : (baseline-(nsigma_start * baseline_sigma)));
  auto end_threshold = ( end_adc_thres > (nsigma_end * baseline_sigma) ? (baseline - end_adc_thres) : (baseline - (nsigma_end * baseline_sigma)));

  std::vector<sbnd::trigger::pmtPulse> pulse_vec;
  sbnd::trigger::pmtPulse pulse; 
  for (auto const &adc : wvfm){
    if ( !fire && ((double)adc) <= start_threshold ){ // if its a new pulse 
      fire = true;
      //vic: i move t_start back one, this helps with porch
      pulse.t_start = counter - 1 > 0 ? counter - 1 : counter;    
    }

    if( fire && ((double)adc) > end_threshold ){ // found end of a pulse
      fire = false;
      //vic: i move t_start forward one, this helps with tail
      pulse.t_end = counter < ((int)wvfm.size())  ? counter : counter - 1;
      pulse_vec.push_back(pulse);
      pulse.area = 0; pulse.peak = 0; pulse.t_start = 0; pulse.t_end = 0; pulse.t_peak = 0;
    }   

    if(fire){ // if we're in a pulse 
      if (fAreaToPE == true) // Add this adc count to the integral
        pulse.area += ((double)adc - baseline);
      if (pulse.peak < ((double)adc - baseline)) { // Found a new maximum
        pulse.peak = ((double)adc - baseline);
        pulse.t_peak = counter;
      }
    }
    counter++;
  }

  if(fire){ // Take care of a pulse that did not finish within the readout window.
    fire = false;
    pulse.t_end = counter - 1;
    pulse_vec.push_back(pulse);
    pulse.area = 0; pulse.peak = 0; pulse.t_start = 0; pulse.t_end = 0; pulse.t_peak = 0;
  }

  std::cout << "number of pulses: " << pulse_vec.size() << std::endl;
  pmtInfo.pulseVec = pulse_vec; 

  for (size_t i_pulse = 0; i_pulse < pulse_vec.size(); ++i_pulse){
    auto &pulse = pulse_vec[i_pulse];
    if (fAreaToPE == true){ pulse.pe = pulse.area/fSPEArea;}
  }
}

// void sbnd::trigger:pmtSoftwareTriggerProducer::SlidingThreshAlgo(){
// }

DEFINE_ART_MODULE(sbnd::trigger::pmtSoftwareTriggerProducer)
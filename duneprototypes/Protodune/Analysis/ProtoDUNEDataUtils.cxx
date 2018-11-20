#include "dune/Protodune/Analysis/ProtoDUNEDataUtils.h"

#include "lardataobj/RawData/RDTimeStamp.h"
#include "lardataobj/RawData/RawDigit.h"

#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"

#include "dune-raw-data/Services/ChannelMap/PdspChannelMapService.h"
#include "dune/DuneObj/ProtoDUNEBeamEvent.h"

protoana::ProtoDUNEDataUtils::ProtoDUNEDataUtils(fhicl::ParameterSet const& p){
  this->reconfigure(p);
}

protoana::ProtoDUNEDataUtils::~ProtoDUNEDataUtils(){

}

void protoana::ProtoDUNEDataUtils::reconfigure(fhicl::ParameterSet const& p){
  fTimingTag = p.get<art::InputTag>("TimingTag");
  fBeamEventTag = p.get<art::InputTag>("BeamEventTag");

  fStrictTOF = p.get<bool>("StrictTOF");
  fStrictCherenkov = p.get<bool>("StrictCherenkov");

  fTOFDistance = p.get<float>("TOFDistance"); // meters
  fMomentumScaleFactor = p.get<float>("MomentumScaleFactor"); 
  fMomentumOffset = p.get<float>("MomentumOffset"); // GeV/c
  fTOFScaleFactor = p.get<float>("TOFScaleFactor");
  fTOFOffset = p.get<float>("TOFOffset"); // ns
}

// Access the trigger information to see if this is a beam trigger
bool protoana::ProtoDUNEDataUtils::IsBeamTrigger(art::Event const & evt) const{

  bool isBeam = false;

  // Accessing the trigger information as done in DataPrepModule
  // The information is stored in the time stamps
  art::Handle<std::vector<raw::RDTimeStamp>> timeStamps;
  evt.getByLabel(fTimingTag,timeStamps);

  // Return false if we have no time stamps
  if(!timeStamps.isValid()) return isBeam;
  // We should only have one RDTimeStamp
  if(timeStamps->size() > 1) return isBeam;
  
  // Access the trigger information. Beam trigger flag = 0xc
  const raw::RDTimeStamp& timeStamp = timeStamps->at(0);
  isBeam = (timeStamp.GetFlags() == 0xc);
  
  return isBeam;
}

// ----------------------------------------------------------------------------
int protoana::ProtoDUNEDataUtils::GetNActiveFembsForAPA(art::Event const & evt, int apa) const {

  // Get raw digits
  art::Handle< std::vector<raw::RawDigit> > RawdigitListHandle;
  std::vector<art::Ptr<raw::RawDigit> > digitlist;
  if (evt.getByLabel("tpcrawdecoder", "daq", RawdigitListHandle))
    art::fill_ptr_vector(digitlist, RawdigitListHandle);

  // Get pd channel map
  art::ServiceHandle<dune::PdspChannelMapService> channelMap;

  // set only saves unique elements
  std::set<int> apaset;

  for(auto const & dptr : digitlist) {
    const raw::RawDigit & digit = *dptr;
    
    // Get the channel number for this digit
    uint32_t chan = digit.Channel();
    int iapa = channelMap->APAFromOfflineChannel(chan);
    if(iapa != apa) continue;
    // Get the channel FEMB and WIB
    int WIB = channelMap->WIBFromOfflineChannel(chan); // 0-4
    int FEMB = channelMap->FEMBFromOfflineChannel(chan); // 1-4
    //int FEMBchan = channelMap->FEMBChannelFromOfflineChannel(chan);
    int iFEMB = ((WIB*4)+(FEMB-1)); //index of the FEMB 0-19

    apaset.insert(iFEMB);
  }

  return (apaset.size());

}
// ----------------------------------------------------------------------------
bool protoana::ProtoDUNEDataUtils::IsGoodBeamlineTrigger(art::Event const & evt) const{
  return true;
}

std::vector<double> protoana::ProtoDUNEDataUtils::GetBeamlineMass(art::Event const & evt) const{
  std::vector<double> tofs;
  std::vector<double> momenta;
  std::vector<double> result;

  std::vector<art::Ptr<beam::ProtoDUNEBeamEvent>> beamVec;
  auto beamHand = evt.getValidHandle<std::vector<beam::ProtoDUNEBeamEvent>>(fBeamEventTag);
  if(beamHand.isValid())
  {
    art::fill_ptr_vector(beamVec, beamHand);
  }

  for(size_t iBeamEvent=0; iBeamEvent < beamVec.size(); iBeamEvent++)
  {
    const beam::ProtoDUNEBeamEvent& beamEvent = *(beamVec.at(iBeamEvent));
    tofs.push_back(beamEvent.GetTOF());

    const std::vector<double> & beamMomenta = beamEvent.GetRecoBeamMomenta();
    for(size_t iMom=0; iMom < beamMomenta.size(); iMom++)
    {
      momenta.push_back(beamEvent.GetRecoBeamMomentum(iMom));
    }
  }
  for(const auto & tof : tofs)
  {
    for(const auto & momentum : momenta)
    {
        double massSquared = std::pow(momentum*fMomentumScaleFactor-fMomentumOffset,2) 
                    * ((tof*fTOFScaleFactor-fTOFOffset)/(fTOFDistance/2.99e8*1e9) - 1.);
        double mass = std::sqrt(massSquared);
        result.push_back(mass);
    }
  }
  return result;
}

protoana::PossibleParticleCands protoana::ProtoDUNEDataUtils::GetCherenkovParticleID(art::Event const & evt, const float beamEnergyGeV) const{
  std::vector<art::Ptr<beam::ProtoDUNEBeamEvent>> beamVec;
  auto beamHand = evt.getValidHandle<std::vector<beam::ProtoDUNEBeamEvent>>(fBeamEventTag);
  if(beamHand.isValid())
  {
    art::fill_ptr_vector(beamVec, beamHand);
  }
  int CKov0Status = -5;
  int CKov1Status = -5;
  for(size_t iBeamEvent=0; iBeamEvent < beamVec.size(); iBeamEvent++)
  {
    const beam::ProtoDUNEBeamEvent& beamEvent = *(beamVec.at(iBeamEvent));
    CKov0Status = beamEvent.GetCKov0Status();
    CKov1Status = beamEvent.GetCKov1Status();
  }
  if (CKov0Status < 0 || CKov1Status < 0)
  {
    if (fStrictCherenkov)
    {
      protoana::PossibleParticleCands result = {false,false,false,false,false};
      return result;
    }
    else
    {
      protoana::PossibleParticleCands result = {true,true,true,true,true};
      return result;
    }
  }
  if (beamEnergyGeV < 2.5)
  {
    if(CKov1Status == 1)
    {
      protoana::PossibleParticleCands result = {true,false,false,false,false};
      return result;
    }
    else
    {
      protoana::PossibleParticleCands result = {false,true,true,true,true};
      return result;
    }
  }
  else if (beamEnergyGeV < 3.5)
  {
    if(CKov0Status == 1 && CKov1Status == 1) // electron
    {
      protoana::PossibleParticleCands result = {true,false,false,false,false};
      return result;
    }
    else if(CKov1Status == 1) // pi/mu
    {
      protoana::PossibleParticleCands result = {false,true,true,false,false};
      return result;
    }
    else // kaon/proton
    {
      protoana::PossibleParticleCands result = {false,false,false,true,true};
      return result;
    }
  }
  else // 6 and 7 GeV
  {
    if(CKov0Status == 1 && CKov1Status == 1) // electron/muon/pion
    {
      protoana::PossibleParticleCands result = {true,true,true,false,false};
      return result;
    }
    else if(CKov1Status == 1) // kaon
    {
      protoana::PossibleParticleCands result = {false,false,false,true,false};
      return result;
    }
    else // proton
    {
      protoana::PossibleParticleCands result = {false,false,false,false,true};
      return result;
    }
  }
}

protoana::PossibleParticleCands protoana::ProtoDUNEDataUtils::GetTOFParticleID(art::Event const & evt, const float beamEnergyGeV) const{
  if (beamEnergyGeV >= 2.5)
  {
    protoana::PossibleParticleCands result = {true,true,true,true,true};
    return result;
  }
  std::vector<art::Ptr<beam::ProtoDUNEBeamEvent>> beamVec;
  auto beamHand = evt.getValidHandle<std::vector<beam::ProtoDUNEBeamEvent>>(fBeamEventTag);
  if(beamHand.isValid())
  {
    art::fill_ptr_vector(beamVec, beamHand);
  }
  float TOF = -9e7;
  for(size_t iBeamEvent=0; iBeamEvent < beamVec.size(); iBeamEvent++)
  {
    const beam::ProtoDUNEBeamEvent& beamEvent = *(beamVec.at(iBeamEvent));
    if (!beamEvent.CheckIsMatched())
    {
      if(fStrictTOF)
      {
        protoana::PossibleParticleCands result = {false,false,false,false,false};
        return result;
      }
      else
      {
        protoana::PossibleParticleCands result = {true,true,true,true,true};
        return result;
      }
    }
    TOF = beamEvent.GetTOF();
    break;
  }
  if (beamEnergyGeV < 1.5)
  {
    if (TOF < 170.)
    {
      protoana::PossibleParticleCands result = {true,true,true,true,false};
      return result;
    }
    else
    {
      protoana::PossibleParticleCands result = {false,false,false,false,true};
      return result;
    }
  }
  else // is 2 GeV
  {
    if (TOF < 160.)
    {
      protoana::PossibleParticleCands result = {true,true,true,true,false};
      return result;
    }
    else
    {
      protoana::PossibleParticleCands result = {false,false,false,false,true};
      return result;
    }
  }
}

protoana::PossibleParticleCands protoana::ProtoDUNEDataUtils::GetBeamlineParticleID(art::Event const & evt, const float beamEnergyGeV) const {
  const auto tof = GetTOFParticleID(evt,beamEnergyGeV);
  const auto chkov = GetCherenkovParticleID(evt,beamEnergyGeV);
  auto result = tof && chkov;
  result.electron = chkov.electron; // disregard tof for electrons
  return result;
}

const std::tuple<double,double,int,int> protoana::ProtoDUNEDataUtils::GetBeamlineVars(art::Event const & evt) const {

  double momentum = -99999.;
  double tof = -99999.;
  int ckov0 = -99999.;
  int ckov1 = -99999.;

  std::vector<art::Ptr<beam::ProtoDUNEBeamEvent>> beamVec;
  auto beamHand = evt.getValidHandle<std::vector<beam::ProtoDUNEBeamEvent>>(fBeamEventTag);
  if(beamHand.isValid())
  {
    art::fill_ptr_vector(beamVec, beamHand);
  }
  for(size_t iBeamEvent=0; iBeamEvent < beamVec.size(); iBeamEvent++)
  {
    const beam::ProtoDUNEBeamEvent& beamEvent = *(beamVec.at(iBeamEvent));
    tof = beamEvent.GetTOF();
    ckov0 = beamEvent.GetCKov0Status();
    ckov1 = beamEvent.GetCKov1Status();

    const std::vector<double> & beamMomenta = beamEvent.GetRecoBeamMomenta();
    if (beamMomenta.size() > 0)
    {
        momentum = beamEvent.GetRecoBeamMomentum(0);
    }
  }
  return std::make_tuple(momentum, tof, ckov0, ckov1);
}

const std::tuple<double,double,int,int,int,double,double,int,int,bool> protoana::ProtoDUNEDataUtils::GetBeamlineVarsAndStatus(art::Event const & evt) const {

  double momentum = -99999.;
  double tof = -99999.;
  int tofChannel = -99999.;
  int ckov0 = -99999.;
  int ckov1 = -99999.;
  double ckov0Pressure = -99999.;
  double ckov1Pressure = -99999.;
  int timingTrigger = -99999.;
  int BITrigger = -99999.;
  bool areBIAndTimingMatched = false;

  std::vector<art::Ptr<beam::ProtoDUNEBeamEvent>> beamVec;
  auto beamHand = evt.getValidHandle<std::vector<beam::ProtoDUNEBeamEvent>>(fBeamEventTag);
  if(beamHand.isValid())
  {
    art::fill_ptr_vector(beamVec, beamHand);
  }
  for(size_t iBeamEvent=0; iBeamEvent < beamVec.size(); iBeamEvent++)
  {
    const beam::ProtoDUNEBeamEvent& beamEvent = *(beamVec.at(iBeamEvent));
    tof = beamEvent.GetTOF();
    tofChannel = beamEvent.GetTOFChan();
    ckov0 = beamEvent.GetCKov0Status();
    ckov1 = beamEvent.GetCKov1Status();
    ckov0Pressure = beamEvent.GetCKov0Pressure();
    ckov1Pressure = beamEvent.GetCKov1Pressure();
    timingTrigger = beamEvent.GetTimingTrigger();
    BITrigger = beamEvent.GetBITrigger();
    areBIAndTimingMatched = beamEvent.CheckIsMatched();

    const std::vector<double> & beamMomenta = beamEvent.GetRecoBeamMomenta();
    if (beamMomenta.size() > 0)
    {
        momentum = beamEvent.GetRecoBeamMomentum(0);
    }
    break;
  }
  return std::make_tuple(momentum, tof, tofChannel, ckov0, ckov1, ckov0Pressure, ckov1Pressure, timingTrigger, BITrigger, areBIAndTimingMatched);
}


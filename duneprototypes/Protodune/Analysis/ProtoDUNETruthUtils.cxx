#include "dune/Protodune/Analysis/ProtoDUNETruthUtils.h"
#include "dune/Protodune/Analysis/ProtoDUNEShowerUtils.h"

#include "larsim/MCCheater/BackTrackerService.h"
#include "larsim/MCCheater/ParticleInventoryService.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/Simulation/SimChannel.h"
#include "art/Framework/Principal/Event.h"

#include "lardata/DetectorInfoServices/DetectorClocksService.h"

#include "TVector3.h"

protoana::ProtoDUNETruthUtils::ProtoDUNETruthUtils(){

}

protoana::ProtoDUNETruthUtils::~ProtoDUNETruthUtils(){

}

// Function to find a weighted and sorted vector of the true particles matched to
// a track. Return an empty vector in case of trouble.
std::vector<std::pair<const simb::MCParticle*, double>>
  protoana::ProtoDUNETruthUtils::GetMCParticleListFromRecoTrack
  (const recob::Track &track, art::Event const & evt, std::string trackModule) const {

  using weightedMCPair = std::pair<const simb::MCParticle*, double>;

  std::vector<weightedMCPair> outVec;

  // We must have MC for this module to make sense
  if(evt.isRealData()) return outVec;

  // Get the reconstructed tracks
  auto allRecoTracks = evt.getValidHandle<std::vector<recob::Track> >(trackModule);

  // We need the association between the tracks and the hits
  const art::FindManyP<recob::Hit> findTrackHits(allRecoTracks, evt, trackModule);

  art::ServiceHandle<cheat::BackTrackerService> bt_serv;
  art::ServiceHandle<cheat::ParticleInventoryService> pi_serv;
  std::unordered_map<const simb::MCParticle*, double> trkIDE;

  // Sum energy contribution by each track ID and compute total track energy
  double track_E = 0;
  for (auto const& h : findTrackHits.at(track.ID()))
  {
    for (auto const & ide : bt_serv->HitToTrackIDEs(h)) // loop over std::vector<sim::TrackIDE>
    {
      const simb::MCParticle* curr_part = pi_serv->TrackIdToParticle_P(ide.trackID);
      trkIDE[curr_part] += ide.energy; // sum energy contribution by each MCParticle
      track_E += ide.energy;
    }
  }

  // Fill and sort the output vector
  for (weightedMCPair const& p : trkIDE)
  {
    outVec.push_back(p);
  }
  std::sort(outVec.begin(), outVec.end(),
        [](weightedMCPair a, weightedMCPair b){ return a.second > b.second;});

  // Normalise the weights by the total track energy.
  if (track_E < 1e-5) { track_E = 1; } // Protect against zero division
  for (weightedMCPair& p : outVec)
  {
    p.second /= track_E;
  }

  return outVec;
}

// Function to find the best matched reconstructed particle tracks to a true
// particle. In case of problems, or if the particle was not reconstruced as a
// track, returns an empty vector.
std::vector<std::pair<const recob::Track*, double>>
  protoana::ProtoDUNETruthUtils::GetRecoTrackListFromMCParticle
  (const simb::MCParticle &part, art::Event const & evt, std::string trackModule) const{

  using weightedTrackPair = std::pair<const recob::Track*, double>;

  std::vector<weightedTrackPair> outVec;

  // We must have MC for this module to make sense
  if(evt.isRealData()) return outVec;

  // Get the reconstructed tracks
  auto allRecoTracks = evt.getValidHandle<std::vector<recob::Track> >(trackModule);

  // We need the association between the tracks and the hits
  const art::FindManyP<recob::Hit> findTrackHits(allRecoTracks, evt, trackModule);

  art::ServiceHandle<cheat::BackTrackerService> bt_serv;
  std::unordered_map<int, double> recoTrack;

  // Record the energy contribution to the MCParticle of every relevant reco track
  double part_E = 0;
  for (recob::Track const & track : *allRecoTracks)
  {
    // This finds all hits that are shared between a reconstructed track and MCParticle
    for (art::Ptr<recob::Hit> const & hptr :
          bt_serv->TrackIdToHits_Ps(part.TrackId(), findTrackHits.at(track.ID())))
    {
      // Loop over hit IDEs to find our particle's part in it
      for (const sim::IDE* ide : bt_serv->HitToSimIDEs_Ps(hptr))
      {
        if (ide->trackID == part.TrackId())
        {
          recoTrack[track.ID()] += ide->energy;
          part_E += ide->energy;
        }
      } // sim::IDE*
    } // art::Ptr<recob::Hit>
  } // const recob::Track&

  // Fill and sort the output vector
  for (std::pair<int, double> const& p : recoTrack)
  {
    auto const trackIt = std::find_if(allRecoTracks->begin(), allRecoTracks->end(),
                               [&](recob::Track tr){ return tr.ID() == p.first; });
    outVec.push_back(std::make_pair(&*trackIt, p.second));
  }
  std::sort(outVec.begin(), outVec.end(),
    [](weightedTrackPair a, weightedTrackPair b){ return a.second > b.second;});

  // Normalise the vector weights
  if (part_E < 1e-5) { part_E = 1; } // Protect against zero division
  for (weightedTrackPair& p : outVec)
  {
    p.second /= part_E;
  }

  return outVec;
}

// Function to find the best matched true particle to a reconstructed particle
// shower. In case of problems, returns a null pointer
std::vector<std::pair<const simb::MCParticle*, double>>
  protoana::ProtoDUNETruthUtils::GetMCParticleListFromRecoShower
  (const recob::Shower &shower, art::Event const & evt, std::string showerModule) const{

  using weightedMCPair = std::pair<const simb::MCParticle*, double>;

  std::vector<weightedMCPair> outVec;

  // We must have MC for this module to make sense
  if(evt.isRealData()) return outVec;

  // Get the reconstructed showers
  auto allRecoShowers = evt.getValidHandle<std::vector<recob::Shower> >(showerModule);

  // We need the association between the showers and the hits
  const art::FindManyP<recob::Hit> findShowerHits(allRecoShowers, evt, showerModule);

  // Find the shower ID via ProtoDUNEShowerUtils
  ProtoDUNEShowerUtils shUtils;
  unsigned int showerIndex = shUtils.GetShowerIndex(shower, evt, showerModule);

  art::ServiceHandle<cheat::BackTrackerService> bt_serv;
  art::ServiceHandle<cheat::ParticleInventoryService> pi_serv;
  std::unordered_map<const simb::MCParticle*, double> trkIDE;

  // Sum energy contribution by each shower ID and compute total shower energy
  double shower_E = 0;
  for (auto const& h : findShowerHits.at(showerIndex))
  {
    for (auto const & ide : bt_serv->HitToEveTrackIDEs(h)) // loop over std::vector<sim::TrackIDE>
    {
      const simb::MCParticle* curr_part = pi_serv->TrackIdToParticle_P(ide.trackID);
      trkIDE[curr_part] += ide.energy; // sum energy contribution by each MCParticle
      shower_E += ide.energy;
    }
  }

  // Fill and sort the output vector
  for (weightedMCPair const& p : trkIDE)
  {
    outVec.push_back(p);
  }
  std::sort(outVec.begin(), outVec.end(),
        [](weightedMCPair a, weightedMCPair b){ return a.second > b.second; });

  // Normalise the weights by the total track energy.
  if (shower_E < 1e-5) { shower_E = 1; } // Protect against zero division
  std::for_each(outVec.begin(), outVec.end(),
        [&](weightedMCPair& p){ p.second /= shower_E; });

  return outVec;
}

// Function to find the best matched reconstructed particle tracks to a true
// particle. In case of problems, or if the particle was not reconstruced as a
// shower, returns an empty vector.
std::vector<std::pair<const recob::Shower*, double>>
  protoana::ProtoDUNETruthUtils::GetRecoShowerListFromMCParticle
  (const simb::MCParticle &part, art::Event const & evt, std::string showerModule) const{

  using weightedShowerPair = std::pair<const recob::Shower*, double>;

  std::vector<weightedShowerPair> outVec;

  // We must have MC for this module to make sense
  if(evt.isRealData()) return outVec;

  // Get the reconstructed showers
  auto allRecoShowers = evt.getValidHandle<std::vector<recob::Shower> >(showerModule);

  // We need the association between the showers and the hits
  const art::FindManyP<recob::Hit> findShowerHits(allRecoShowers, evt, showerModule);

  art::ServiceHandle<cheat::BackTrackerService> bt_serv;
  ProtoDUNEShowerUtils shUtils;
  std::unordered_map<int, double> recoShower;

  // Record the energy contribution to the MCParticle of every relevant reco shower
  double part_E = 0;
  for (recob::Shower const & shower : *allRecoShowers)
  {
    const unsigned showerIndex = shUtils.GetShowerIndex(shower, evt, showerModule);
    // Since MCParticles do not have shower hits associated with them, loop over all shower hits
    for (art::Ptr<recob::Hit> hit : findShowerHits.at(showerIndex))
    {
      // Loop over hit IDEs to find our particle's part in it
      for (const sim::TrackIDE& ide : bt_serv->HitToEveTrackIDEs(hit))
      {
        if (ide.trackID == part.TrackId())
        {
          recoShower[showerIndex] += ide.energy;
          part_E += ide.energy;
        }
      } // sim::IDE*
    } // art::Ptr<recob::Hit>
  } // const recob::Shower&

  // Fill and sort the output vector
  for (std::pair<int, double> const& p : recoShower)
  {
    auto const showerIt = std::find_if(allRecoShowers->begin(), allRecoShowers->end(),
      [&](recob::Shower sh){ return shUtils.GetShowerIndex(sh, evt, showerModule) == p.first; });
    outVec.push_back(std::make_pair(&*showerIt, p.second));
  }
  std::sort(outVec.begin(), outVec.end(),
    [](weightedShowerPair a, weightedShowerPair b){ return a.second > b.second;});

  // Normalise the vector weights
  if (part_E < 1e-5) { part_E = 1; } // Protect against zero division
  std::for_each(outVec.begin(), outVec.end(),
          [&](weightedShowerPair& p){ p.second /= part_E; });

  return outVec;
}

// Function to find the best matched true particle to a reconstructed particle
// track. In case of problems, returns a null pointer
const simb::MCParticle* protoana::ProtoDUNETruthUtils::GetMCParticleFromRecoTrack
  (const recob::Track &track, art::Event const & evt, std::string trackModule) const{

  const simb::MCParticle* outPart = 0x0;

  const std::vector<std::pair<const simb::MCParticle*, double>> inVec =
    GetMCParticleListFromRecoTrack(track, evt, trackModule);

  if (inVec.size() != 0) outPart = inVec[0].first;

  return outPart;
}

// Function to find the best matched reconstructed track to a true particle. In
// case of problems, or if the true particle is not a primary contributor to any
// track, return a null pointer
const recob::Track* protoana::ProtoDUNETruthUtils::GetRecoTrackFromMCParticle
  (const simb::MCParticle &part, art::Event const & evt, std::string trackModule) const {

  const recob::Track* outTrack = 0x0;

  // We must have MC for this module to make sense
  if(evt.isRealData()) return outTrack;

  // Get the list of contributing tracks for the MCParticle and see if any of
  // them have the MCParticle as a primary contributor.
  for (std::pair<const recob::Track*, double> p :
                        GetRecoTrackListFromMCParticle(part, evt, trackModule))
  {
    const recob::Track* tr = p.first;
    if (GetMCParticleFromRecoTrack(*tr, evt, trackModule)->TrackId() == part.TrackId())
    {
      outTrack = tr;
      break;
    }
  }

  return outTrack;
}

// Function to find the best matched true particle to a reconstructed particle
// shower. In case of problems, returns a null pointer
const simb::MCParticle* protoana::ProtoDUNETruthUtils::GetMCParticleFromRecoShower
  (const recob::Shower &shower, art::Event const & evt, std::string showerModule) const{

  const simb::MCParticle* outPart = 0x0;

  const std::vector<std::pair<const simb::MCParticle*, double>> inVec =
    GetMCParticleListFromRecoShower(shower, evt, showerModule);

  if (inVec.size() != 0) outPart = inVec[0].first;

  return outPart;
}

// Function to find the best matched reconstructed shower to a true particle. In
// case of problems, or if the true particle is not a primary contributor to any
// shower, returns a null pointer
const recob::Shower* protoana::ProtoDUNETruthUtils::GetRecoShowerFromMCParticle
  (const simb::MCParticle &part, art::Event const & evt, std::string showerModule) const {

  const recob::Shower* outShower = 0x0;

  // We must have MC for this module to make sense
  if(evt.isRealData()) return outShower;

  // Get the list of contributing showers for the MCParticle and see if any of
  // them have the MCParticle as a primary contributor.
  for (std::pair<const recob::Shower*, double> p :
                        GetRecoShowerListFromMCParticle(part, evt, showerModule))
  {
    const recob::Shower* tr = p.first;
    if (GetMCParticleFromRecoShower(*tr, evt, showerModule)->TrackId() == part.TrackId())
    {
      outShower = tr;
      break;
    }
  }
  
  return outShower;
}

const simb::MCParticle* protoana::ProtoDUNETruthUtils::MatchPduneMCtoG4( const simb::MCParticle & pDunePart, const art::Event & evt )
{  // Function that will match the protoDUNE MC particle to the Geant 4 MC particle, and return the matched particle (or a null pointer).

  // Find the energy of the procided MC Particle
  double pDuneEnergy = pDunePart.E();

  // Get list of the g4 particles. plist should be a std::map< int, simb::MCParticle* >
  art::ServiceHandle< cheat::ParticleInventoryService > pi_serv;
  const sim::ParticleList & plist = pi_serv->ParticleList();

  // Check if plist is empty
  if ( !plist.size() ) {
    std::cerr << "\n\n#####################################\n"
              << "\nEvent " << evt.id().event() << "\n"
              << "sim::ParticleList from cheat::ParticleInventoryService is empty\n"
              << "A null pointer will be returned\n"
              << "#####################################\n\n";
    return nullptr;
  }

  // Go through the list of G4 particles
  for ( auto partIt = plist.begin() ; partIt != plist.end() ; partIt++ ) {

    const simb::MCParticle* pPart = partIt->second;
    if (!pPart) {
      std::cerr << "\n\n#####################################\n"
                << "\nEvent " << evt.id().event() << "\n"
                << "GEANT particle #" << partIt->first << " returned a null pointer\n"
                << "This is not necessarily bad. It just means at least one\n"
                << "of the G4 particles returned a null pointer. It may well\n"
                << "have still matched a PD particle and a G4 particle.\n"
                << "#####################################\n\n";
      continue;
    }

    // If the initial energy of the g4 particle is very close to the energy of the protoDUNE particle, call it a day and have a cuppa.
    if ( (pDunePart.PdgCode() == pPart->PdgCode()) && fabs(pPart->E() - pDuneEnergy) < 0.00001 ) {
      return pPart;
    }

  }  // G4 particle list loop end.

  std::cout << "No G4 particle was matched for Event " << evt.id().event() << ". Null pointer returned\n";
  return nullptr;

}  // End MatchPduneMCtoG4

const simb::MCParticle* protoana::ProtoDUNETruthUtils::GetGeantGoodParticle(const simb::MCTruth &genTruth, const art::Event &evt) const{

  // Get the good particle from the MCTruth
  simb::MCParticle goodPart;
  bool found = false;
  for(int t = 0; t < genTruth.NParticles(); ++t){
    simb::MCParticle part = genTruth.GetParticle(t);
    if(part.Process() == "primary"){
      goodPart = part;
      found = true;
      break;
    }
  }

  if(!found){
    std::cerr << "No good particle found, returning null pointer" << std::endl;
    return nullptr;
  }

  // Now loop over geant particles to find the one that matches
  // Get list of the g4 particles. plist should be a std::map< int, simb::MCParticle* >
  art::ServiceHandle< cheat::ParticleInventoryService > pi_serv;
  const sim::ParticleList & plist = pi_serv->ParticleList();

  for(auto const part : plist){
    if((goodPart.PdgCode() == part.second->PdgCode()) && fabs(part.second->E() - goodPart.E()) < 1e-5){
      return part.second;
    }
  }

  // If we get here something has gone wrong
  std::cerr << "No G4 version of the good particle was found, returning null pointer" << std::endl;
  return nullptr;

}

// Converting times in LArSoft can be a bit of a minefield. These functions convert true times in ns
// to pandora times in ns
const float protoana::ProtoDUNETruthUtils::ConvertTrueTimeToPandoraTimeNano(const simb::MCParticle &part) const{
  return ConvertTrueTimeToPandoraTimeNano(part.T());
}

const float protoana::ProtoDUNETruthUtils::ConvertTrueTimeToPandoraTimeNano(const float trueTime) const{
  return 1000. * ConvertTrueTimeToPandoraTimeMicro(trueTime);
}

// Microsecond versions
const float protoana::ProtoDUNETruthUtils::ConvertTrueTimeToPandoraTimeMicro(const simb::MCParticle &part) const{
  return ConvertTrueTimeToPandoraTimeMicro(part.T());
}

const float protoana::ProtoDUNETruthUtils::ConvertTrueTimeToPandoraTimeMicro(const float trueTime) const{

  // Use the clocks service to account for the offset between the Geant4 time and the electronics clock
  auto const* detclock = lar::providerFrom<detinfo::DetectorClocksService>();

  return detclock->G4ToElecTime(trueTime);
}

// Get process key.
int protoana::ProtoDUNETruthUtils::GetProcessKey(std::string process){

  if(process.compare("primary") == 0)                    return 0;
  else if(process.compare("hadElastic") == 0)            return 1;
  else if(process.compare("pi-Inelastic") == 0)          return 2;
  else if(process.compare("pi+Inelastic") == 0)          return 3;
  else if(process.compare("kaon-Inelastic") == 0)        return 4;
  else if(process.compare("kaon+Inelastic") == 0)        return 5;
  else if(process.compare("protonInelastic") == 0)       return 6;
  else if(process.compare("neutronInelastic") == 0)      return 7;
  else if(process.compare("kaon0SInelastic") == 0)       return 8;
  else if(process.compare("kaon0LInelastic") == 0)       return 9;
  else if(process.compare("lambdaInelastic") == 0)       return 10;
  else if(process.compare("omega-Inelastic") == 0)       return 11;
  else if(process.compare("sigma+Inelastic") == 0)       return 12;
  else if(process.compare("sigma-Inelastic") == 0)       return 13;
  else if(process.compare("sigma0Inelastic") == 0)       return 14;
  else if(process.compare("xi-Inelastic") == 0)          return 15;
  else if(process.compare("xi0Inelastic") == 0)          return 16;
  else if(process.compare("anti_protonInelastic") == 0)  return 20;
  else if(process.compare("anti_neutronInelastic") == 0) return 21;
  else if(process.compare("anti_lambdaInelastic") == 0)  return 22;
  else if(process.compare("anti_omega-Inelastic") == 0)  return 23;
  else if(process.compare("anti_sigma+Inelastic") == 0)  return 24;
  else if(process.compare("anti_sigma-Inelastic") == 0)  return 25;
  else if(process.compare("anti_xi-Inelastic") == 0)     return 26;
  else if(process.compare("anti_xi0Inelastic") == 0)     return 27;

  else if(process.compare("Decay") == 0)                 return 30;
  else if(process.compare("FastScintillation") == 0)     return 31;
  else if(process.compare("nKiller") == 0)               return 32; // Remove unwanted neutrons: neutron kinetic energy threshold (default 0) or time limit for neutron track
  else if(process.compare("nCapture") == 0)              return 33; // Neutron capture

  else if(process.compare("compt") == 0)                 return 40; // Compton Scattering
  else if(process.compare("rayleigh") == 0)              return 41; // Rayleigh Scattering
  else if(process.compare("phot") == 0)                  return 42; // Photoelectric Effect
  else if(process.compare("conv") == 0)                  return 43; // Pair production
  else if(process.compare("CoupledTransportation") == 0) return 44; //

  else return -1;
}

// Get estimated particle energy deposit. The G4 trackId must be provided
double protoana::ProtoDUNETruthUtils::GetDepEnergyMC(const art::Event &evt, geo::GeometryCore const * fGeom, int trackid, int whichview) const {

  double edep = 0.0;

  art::Handle< std::vector<sim::SimChannel> > simchannelHandle;
  if(evt.getByLabel("largeant", simchannelHandle)){
    // Loop over sim channels
    for(auto const& simchannel : (*simchannelHandle)){
      // Only keep channels in the selected view
      if(fGeom->View(simchannel.Channel()) != whichview) continue;
      // Get all time slices
      auto const& alltimeslices = simchannel.TDCIDEMap();
      // Loop over time slices
      for(auto const& tslice : alltimeslices){
	auto const& simide = tslice.second;
	// Loop over energy deposits
	for(auto const& eDep : simide){
	  if(eDep.trackID == trackid || eDep.trackID == -trackid)
	    edep += eDep.energy;
	}
      }
    }
  }

  return edep;

}

// Get first trajectory point in TPC active volume
int protoana::ProtoDUNETruthUtils::GetFirstTrajectoryPointInTPCActiveVolume(const simb::MCParticle& mcpart, double tpcactiveXlow, double tpcactiveXhigh, double tpcactiveYlow, double tpcactiveYhigh, double tpcactiveZlow, double tpcactiveZhigh){

  int firstpoint = -999;
  for(unsigned int i = 0; i < mcpart.NumberTrajectoryPoints(); ++i) {
    if(mcpart.Vx(i) >= tpcactiveXlow && mcpart.Vx(i) <= tpcactiveXhigh && mcpart.Vy(i) >= tpcactiveYlow && mcpart.Vy(i) <= tpcactiveYhigh && mcpart.Vz(i) >= tpcactiveZlow && mcpart.Vz(i) <= tpcactiveZhigh){

      firstpoint = i;
      break;
    }
  }

  return firstpoint;
}

// Get MC Particle length in TPC active volume
double protoana::ProtoDUNETruthUtils::GetMCParticleLengthInTPCActiveVolume(const simb::MCParticle& mcpart, double tpcactiveXlow, double tpcactiveXhigh, double tpcactiveYlow, double tpcactiveYhigh, double tpcactiveZlow, double tpcactiveZhigh){

  double length = 0.0;
  int firstpoint = GetFirstTrajectoryPointInTPCActiveVolume(mcpart, tpcactiveXlow, tpcactiveXhigh, tpcactiveYlow, tpcactiveYhigh, tpcactiveZlow, tpcactiveZhigh);

  if(firstpoint < 0) return length;

  TVector3 pos =  mcpart.Position(firstpoint).Vect();
  for(unsigned int i = firstpoint+1; i < mcpart.NumberTrajectoryPoints(); ++i) {
    if(mcpart.Vx(i) >= tpcactiveXlow && mcpart.Vx(i) <= tpcactiveXhigh && mcpart.Vy(i) >= tpcactiveYlow && mcpart.Vy(i) <= tpcactiveYhigh && mcpart.Vz(i) >= tpcactiveZlow && mcpart.Vz(i) <= tpcactiveZhigh){

      pos -= mcpart.Position(i).Vect();
      length += pos.Mag();
      pos = mcpart.Position(i).Vect();
    }
  }

  return length;
}

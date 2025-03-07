//----------------------------------------------------------------------
/// \file
/// \page Example07 07 - subtracting jet background contamination
///
/// fastjet subtraction example program. 
///
/// run it with    : ./07-subtraction < data/Pythia-Zp2jets-lhc-pileup-1ev.dat
///
/// Source code: 07-subtraction.cc
//----------------------------------------------------------------------

//FJSTARTHEADER
// $Id$
//
// Copyright (c) 2005-2024, Matteo Cacciari, Gavin P. Salam and Gregory Soyez
//
//----------------------------------------------------------------------
// This file is part of FastJet.
//
//  FastJet is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  The algorithms that underlie FastJet have required considerable
//  development. They are described in the original FastJet paper,
//  hep-ph/0512210 and in the manual, arXiv:1111.6097. If you use
//  FastJet as part of work towards a scientific publication, please
//  quote the version you use and include a citation to the manual and
//  optionally also to hep-ph/0512210.
//
//  FastJet is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with FastJet. If not, see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------
//FJENDHEADER

#include "fastjet/PseudoJet.hh"
#include "fastjet/ClusterSequenceArea.hh"
#include "fastjet/Selector.hh"
#include "fastjet/tools/JetMedianBackgroundEstimator.hh"
#include "fastjet/tools/Subtractor.hh" 
#include <iostream> // needed for io
#include <fastjet/config.h>  // for the FASTJET_VERSION_NUMBER preprocessor symbol

using namespace std;
using namespace fastjet;

int main(){
  
  // read in input particles
  //
  // since we use here simulated data we can split the hard event
  // from the full (i.e. with pileup added) one
  //----------------------------------------------------------

  vector<PseudoJet> hard_event, full_event;
  
  // read in input particles. Keep the hard event generated by PYTHIA
  // separated from the full event, so as to be able to gauge the
  // "goodness" of the subtraction from the full event, which also
  // includes pileup
  double particle_maxrap = 5.0;

  string line;
  int  nsub  = 0; // counter to keep track of which sub-event we're reading
  while (getline(cin, line)) {
    istringstream linestream(line);
    // take substrings to avoid problems when there are extra "pollution"
    // characters (e.g. line-feed).
    if (line.substr(0,4) == "#END") {break;}
    if (line.substr(0,9) == "#SUBSTART") {
      // if more sub events follow, make copy of first one (the hard one) here
      if (nsub == 1) hard_event = full_event;
      nsub += 1;
    }
    if (line.substr(0,1) == "#") {continue;}
    double px,py,pz,E;
    linestream >> px >> py >> pz >> E;
    // you can construct 
    PseudoJet particle(px,py,pz,E);

    // push event onto back of full_event vector
    if (abs(particle.rap()) <= particle_maxrap) full_event.push_back(particle);
  }

  // if we have read in only one event, copy it across here...
  if (nsub == 1) hard_event = full_event;

  // if there was nothing in the event 
  if (nsub == 0) {
    cerr << "Error: read empty event\n";
    exit(-1);
  }
  
  
  // create a jet definition for the clustering
  // We use the anti-kt algorithm with a radius of 0.5
  //----------------------------------------------------------
  double R = 0.5;
  JetDefinition jet_def(antikt_algorithm, R);

  // create an area definition for the clustering
  //----------------------------------------------------------
  // ghosts should go up to the acceptance of the detector or
  // (with infinite acceptance) at least 2R beyond the region
  // where you plan to investigate jets.
  double ghost_maxrap = 6.0;
  GhostedAreaSpec area_spec(ghost_maxrap);
  AreaDefinition area_def(active_area, area_spec);

  // run the jet clustering with the above jet and area definitions
  // for both the hard and full event
  //
  // We retrieve the jets above 7 GeV in both case (note that the
  // 7-GeV cut we be applied again later on after we subtract the jets
  // from the full event)
  // ----------------------------------------------------------
  ClusterSequenceArea clust_seq_hard(hard_event, jet_def, area_def);
  ClusterSequenceArea clust_seq_full(full_event, jet_def, area_def);

  double ptmin = 7.0;
  vector<PseudoJet> hard_jets = sorted_by_pt(clust_seq_hard.inclusive_jets(ptmin));
  vector<PseudoJet> full_jets = sorted_by_pt(clust_seq_full.inclusive_jets(ptmin));

  // Now turn to the estimation of the background (for the full event)
  //
  // There are different ways to do that. In general, this also
  // requires clustering the particles that will be handled internally
  // in FastJet. 
  //
  // The suggested way to proceed is to use a BackgroundEstimator
  // constructed from the following 3 arguments:
  //  - a jet definition used to cluster the particles.
  //    . We strongly recommend using the kt or Cambridge/Aachen
  //      algorithm (a warning will be issued otherwise)
  //    . The choice of the radius is a bit more subtle. R=0.4 has
  //      been chosen to limit the impact of hard jets; in samples of
  //      dominantly sparse events it may cause the UE/pileup to be
  //      underestimated a little, a slightly larger value (0.5 or
  //      0.6) may be better.
  //  - An area definition for which we recommend the use of explicit
  //    ghosts (i.e. active_area_explicit_ghosts)
  //    As mentionned in the area example (06-area.cc), ghosts should
  //    extend sufficiently far in rapidity to cover the jets used in
  //    the computation of the background (see also the comment below)
  //  - A Selector specifying the range over which we will keep the
  //    jets entering the estimation of the background (you should
  //    thus make sure the ghosts extend far enough in rapidity to
  //    cover the range, a warning will be issued otherwise).
  //    In this particular example, the two hardest jets in the event
  //    are removed from the background estimation
  // ----------------------------------------------------------
  JetDefinition jet_def_bkgd(kt_algorithm, 0.4);
  AreaDefinition area_def_bkgd(active_area_explicit_ghosts, 
			       GhostedAreaSpec(ghost_maxrap));
  Selector selector = SelectorAbsRapMax(4.5) * (!SelectorNHardest(2));
  JetMedianBackgroundEstimator bkgd_estimator(selector, jet_def_bkgd, area_def_bkgd);

  // To help manipulate the background estimator, we also provide a
  // transformer that allows to apply directly the background
  // subtraction on the jets. This will use the background estimator
  // to compute rho for the jets to be subtracted.
  // ----------------------------------------------------------
  Subtractor subtractor(&bkgd_estimator);
  
  // since FastJet 3.1.0, rho_m is supported natively in background
  // estimation (both JetMedianBackgroundEstimator and
  // GridMedianBackgroundEstimator).
  //
  // For backward-compatibility reasons its use is by default switched off
  // (as is the enforcement of m>0 for the subtracted jets). The
  // following 2 lines of code switch these on. They are strongly
  // recommended and should become the default in future versions of
  // FastJet.
  //
  // Note that we also illustrate the use of the
  // FASTJET_VERSION_NUMBER macro
#if FASTJET_VERSION_NUMBER >= 30100
  subtractor.set_use_rho_m(true);
  subtractor.set_safe_mass(true);
#endif

  // Finally, once we have an event, we can just tell the background
  // estimator to use that list of particles
  // This could be done directly when declaring the background
  // estimator but the usage below can more easily be accomodated to a
  // loop over a set of events.
  // ----------------------------------------------------------
  bkgd_estimator.set_particles(full_event);

  // show a summary of what was done so far
  //  - the description of the algorithms, areas and ranges used
  //  - the background properties
  //  - the jets in the hard event
  //----------------------------------------------------------
  cout << "Main clustering:" << endl;
  cout << "  Ran:   " << jet_def.description() << endl;
  cout << "  Area:  " << area_def.description() << endl;
  cout << "  Particles up to |y|=" << particle_maxrap << endl;
  cout << endl;

  cout << "Background estimation:" << endl;
  cout << "  " << bkgd_estimator.description() << endl << endl;;
  cout << "  Giving, for the full event" << endl;
  BackgroundEstimate bkgd_estimate = bkgd_estimator.estimate();
  cout << "    rho     = " << bkgd_estimate.rho()   << endl;
  cout << "    sigma   = " << bkgd_estimate.sigma() << endl; 
  cout << "    rho_m   = " << bkgd_estimate.rho_m()   << endl;
  cout << "    sigma_m = " << bkgd_estimate.sigma_m() << endl; 
  cout << endl;

  cout << "Jets above " << ptmin << " GeV in the hard event (" << hard_event.size() << " particles)" << endl;
  cout << "---------------------------------------\n";
  printf("%5s %15s %15s %15s %15s %15s\n","jet #", "rapidity", "phi", "pt", "m", "area");
   for (unsigned int i = 0; i < hard_jets.size(); i++) {
    printf("%5u %15.8f %15.8f %15.8f %15.8f %15.8f\n", i,
	   hard_jets[i].rap(), hard_jets[i].phi(), hard_jets[i].pt(), hard_jets[i].m(),
	   hard_jets[i].area());
  }
  cout << endl;

  // Once the background properties have been computed, subtraction
  // can be applied on the jets. Subtraction is performed on the
  // full 4-vector
  //
  // We output the jets before and after subtraction
  // ----------------------------------------------------------
  cout << "Jets above " << ptmin << " GeV in the full event (" << full_event.size() << " particles)" << endl;
  cout << "---------------------------------------\n";
  printf("%5s %15s %15s %15s %15s %15s %15s %15s %15s %15s\n","jet #", "rapidity", "phi", "pt", "m", "area", "rap_sub", "phi_sub", "pt_sub", "m_sub");
  unsigned int idx=0;

  // get the subtracted jets
  vector<PseudoJet> subtracted_jets = subtractor(full_jets);

  for (unsigned int i=0; i<full_jets.size(); i++){
    // re-apply the pt cut
    if (subtracted_jets[i].pt2() >= ptmin*ptmin){
      printf("%5u %15.8f %15.8f %15.8f %15.8f %15.8f %15.8f %15.8f %15.8f %15.8f\n", idx,
	     full_jets[i].rap(), full_jets[i].phi(), full_jets[i].pt(), full_jets[i].m(),
	     full_jets[i].area(),
	     subtracted_jets[i].rap(), subtracted_jets[i].phi(), 
	     subtracted_jets[i].pt(), 
	     subtracted_jets[i].m());
      idx++;
    }
  }

  return 0;
}

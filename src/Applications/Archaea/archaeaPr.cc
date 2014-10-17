#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>
#include <cmath>

#include <getopt.h>
#include <unistd.h>
#include <time.h>

#include <tvmet/Vector.h>

#include "VoomMath.h"
#include "Node.h"
#include "ProteinLennardJones.h"
#include "ProteinBody.h"
#include "MontecarloProtein.h"
#include "Utils/PrintingProtein.h"

using namespace voom;
using namespace std;

int main(int argc, char* argv[])
{
  // -------------------------------------------------------------------
  // Setup
  // -------------------------------------------------------------------
  time_t start, end;
  double dif;
  time(&start);

  if(argc < 2){
    cout << "Input file missing." << endl;
    return(0);
  }

  // File names
  string parameterFileName = argv[1];
  string modelName;
  string outputFileName;
  string initialConf;
  int ICprovided = 0;
  double ICtol = 1.0e-5;
  int VTKflag = 0;
  double InflationFactor = 1.0;
 
  // Potential input parameters
  double PotentialSearchRF = 1.0;
  double epsilon = 1.0;
  double sigma = 1.0;
  double Rshift = 1.0;

  // Connectivity search
  double RconnSF = 1.0;

  // Monte Carlo solver parameters
  int nMCsteps = 1000;
  double T01, T02, FinalRatio;
  int CompNeighInterval;
  int MCmethod = 0;
  double ResetT = -1.0;

  // Number of nodes per protein
  int NnodePr = 10;
  int PrMaxNum = 10;
  int PrintEvery = 10;

  // Other Analysis options
  int StepWise = 0;


  // Reading input from file passed as argument
  ifstream inp;
  inp.open(parameterFileName.c_str(), ios::in);
  if (!inp) {
    cout << "Cannot open input file: " << parameterFileName << endl;
    return(0);
  }
  string temp;
  
  inp >> temp >> modelName;
  inp >> temp >> outputFileName;
  inp >> temp >> initialConf;
  inp >> temp >> ICprovided;
  inp >> temp >> ICtol;
  inp >> temp >> VTKflag;
  inp >> temp >> InflationFactor;
  inp >> temp >> PotentialSearchRF;
  inp >> temp >> epsilon;
  inp >> temp >> sigma;
  inp >> temp >> Rshift;
  inp >> temp >> RconnSF;
  inp >> temp >> nMCsteps;
  inp >> temp >> T01;
  inp >> temp >> T02;
  inp >> temp >> FinalRatio;
  inp >> temp >> CompNeighInterval;
  inp >> temp >> MCmethod;
  inp >> temp >> ResetT;
  inp >> temp >> NnodePr;
  inp >> temp >> PrMaxNum;
  inp >> temp >> PrintEvery;
  inp >> temp >> StepWise;
  
  inp.close();

  // List input parameters
  cout << " modelName               : " << modelName         << endl
       << " outputFileName          : " << outputFileName    << endl
       << " initialConfiguration    : " << initialConf       << endl
       << " IC provided             : " << ICprovided        << endl
       << " IC tol                  : " << ICtol             << endl
       << " VTK flag                : " << VTKflag           << endl
       << " InflationFactor         : " << InflationFactor   << endl
       << " Potential Search factor : " << PotentialSearchRF << endl
       << " epsilon                 : " << epsilon           << endl
       << " sigma                   : " << sigma             << endl
       << " Rshift                  : " << Rshift            << endl
       << " Rconn search radius     : " << RconnSF           << endl
       << " MC steps                : " << nMCsteps          << endl
       << " MC T01                  : " << T01               << endl
       << " MC T02                  : " << T02               << endl
       << " MC FinalRatio           : " << FinalRatio        << endl
       << " MC ComputeNeighInterval : " << CompNeighInterval << endl
       << " MC method               : " << MCmethod          << endl
       << " MC reset T              : " << ResetT            << endl
       << " Number of nodes per Pr  : " << NnodePr           << endl
       << " Max number of Pr        : " << PrMaxNum          << endl
       << " Print .vtk file every   : " << PrintEvery        << endl
       << " Step wise change in T   : " << StepWise          << endl;







  // -------------------------------------------------------------------
  // Read mesh and setup geometric model
  // -------------------------------------------------------------------
  // Read in the mesh and, if provided the initial configuration
  vector<DeformationNode<3>::Point > IC;
  unsigned NumIC = 0;
  vector<uint > ICcheck;
  if (ICprovided == 1) {
    ifstream ifsIC;
    ifsIC.open(initialConf.c_str(), ios::in);
    if (!ifsIC) {
      cout << "Cannot open input file: " << initialConf << endl;
      exit(0);
    }
    
    string line;
    ifsIC >> line;
    ifsIC >> NumIC;
    ICcheck.assign(NumIC, 1);
    for(uint i = 0; i < NumIC; i++) {
      uint temp = 0;
      DeformationNode<3>::Point xIC;
      ifsIC >> xIC(0) >> xIC(1) >> xIC(2); 
      IC.push_back(xIC);
    }
    ifsIC.close();
  } // end of ICprovided loop
  cout << "NumIC = " << IC.size() << endl;

  // Create input stream
  ifstream ifs;
  ifs.open(modelName.c_str(), ios::in);
  if (!ifs) {
    cout << "Cannot open input file: " << modelName << endl;
    exit(0);
  }
 
  

  // Create vector of nodes
  unsigned int dof = 0, npts = 0, NumDoF = 0;
  vector<NodeBase* > nodes;
  vector<DeformationNode<3>* > defNodes;

  // Input .vtk or inp files containing nodes and connectivities
  string token;
  ifs >> token;
  ifs >> npts; 
  NumDoF = npts*3; // Assumed all nodes have 3 DoF
  nodes.reserve(npts);
  defNodes.reserve(npts);
  vector<ProteinNode *> Proteins;

  // read in points
  uint NextProtein = 0;
  for(uint i = 0; i < npts; i++) {
    DeformationNode<3>::Point x;
    ifs >> x(0) >> x(1) >> x(2); 
    
    NodeBase::DofIndexMap idx(3);
    for(uint j = 0; j < 3; j++) {
      idx[j] = dof++;
    }
    
    // Inflate/Deflate vessel if necessary
    x *= InflationFactor;

    DeformationNode<3>* n = new DeformationNode<3>(i,idx,x);
    
    if (ICprovided == 1) {
      if (VTKflag == 1) {
	if (i < NumIC) {
	  ProteinNode * PrNode = new ProteinNode(n);
	  Proteins.push_back(PrNode);
	}
      }
      else if (VTKflag == 2) {
	for (uint k = 0; k < NumIC; k++) {
	  if (tvmet::norm2(IC[k] - x) < ICtol && ICcheck[k] == 1) {
	    ProteinNode * PrNode = new ProteinNode(n);
	    Proteins.push_back(PrNode);
	    ICcheck[k] = 0;
	    break;
	  }
	}
      } 
    }
    else {
      if (i == NextProtein && Proteins.size() < PrMaxNum) { // create a protein every NodePr nodes and additional conditions
	// if (x(2) > 1.0) { // just for cone example - start from proteins packed close to the tip
	ProteinNode * PrNode = new ProteinNode(n);
	Proteins.push_back(PrNode);
	NextProtein += (rand() % NnodePr) + 1;
	// }
      }
    }
      
    nodes.push_back( n );
    defNodes.push_back( n );
   
  } // end of reading points

  ICcheck.clear();
   
  // Read in triangle connectivities
  vector< tvmet::Vector<int,3> > connectivities;
  tvmet::Vector<int,3> ct;
  uint ntri = 0, ElemNum = 0, tmp = 0;
  map<DeformationNode<3> *, vector<DeformationNode<3> *> > PossibleHosts;

  ifs >> token;
  ifs >> ntri;
  connectivities.reserve(ntri);
  for (uint i = 0; i < ntri; i++)
  {
    ifs >> ElemNum;
    ifs >> ct(0) >> ct(1) >> ct(2);
    connectivities.push_back(ct);
  } // end of reading triangle connectivities

  // build possible hosts table
  for(uint i = 0; i < npts; i++)
  {
    set<DeformationNode<3> *> setHosts;
    for (uint j = 0; j < ntri; j++) {
      ct = connectivities[j];
      if (i == ct(0) || i == ct(1) || i == ct(2) ) {
	setHosts.insert( defNodes[ct(0)] );
	setHosts.insert( defNodes[ct(1)] );
	setHosts.insert( defNodes[ct(2)] );
      }
    }
    
    vector<DeformationNode<3> *> vectorHosts;
    for (set<DeformationNode<3> *>::iterator pHost = setHosts.begin();
	 pHost != setHosts.end(); pHost++) {
      vectorHosts.push_back(*pHost);
    }

    PossibleHosts[ defNodes[i] ] = vectorHosts;
  }
  
  cout << "Number of nodes     = " << nodes.size()          << endl
       << "Number of proteins  = " << Proteins.size()       << endl
       << "Number of elements  = " << connectivities.size() << endl;

  // Close mesh file in inp format
  ifs.close();
  






  // Initiliaze potential material
  ProteinLennardJones Mat(epsilon, sigma);

  // Then initialize potential body
  ProteinBody * PrBody = new ProteinBody(Proteins, &Mat, PotentialSearchRF);  
  PrBody->compute(true, false, false);
  cout << "Initial protein body energy = " << PrBody->energy() << endl;






  
  // Initialize printing utils
  cout << "Seraching connectivity over R = " <<  RconnSF << endl;
  PrintingProtein PrintArchaea(modelName, outputFileName+"iter", nodes, connectivities, Proteins, RconnSF);
  
  // Initialize Montecarlo Solver
  MontecarloProtein MCsolver(Proteins, PrBody, PossibleHosts, MCmethod, &PrintArchaea, ResetT, PrintEvery, nMCsteps);
  if (StepWise == 0) {
    MCsolver.SetTempSchedule(MCsolver.EXPONENTIAL, T01, T02, FinalRatio); }
  // MCsolver.SetTempSchedule(MCsolver.LINEAR, T01, T02, FinalRatio); }
  else {
    MCsolver.SetTempSchedule(MCsolver.STEPWISE, T01, T02, FinalRatio);
  }

  MCsolver.solve(CompNeighInterval, PotentialSearchRF);

 





  // End of program
  time (&end);
  dif = difftime (end,start);
  cout << endl << "All done :) in " << dif  << " s" << endl;
    
  // -------------------------------------------------------------------
  // Clean up
  // -------------------------------------------------------------------
  // delete shellBody;
  delete PrBody;
  
  for (uint i = 0; i<Proteins.size(); i++)
  {
    delete Proteins[i];
  }

  for (uint i = 0; i<nodes.size(); i++)
  {
    delete nodes[i];
  }
  
  return (0);  
}
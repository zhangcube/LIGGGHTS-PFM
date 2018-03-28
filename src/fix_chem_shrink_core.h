/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations
   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com
   Copyright 2015-     JKU Linz
   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov
   This software is distributed under the GNU General Public License.
   See the README file in the top-level directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing authors:
   Thomas Lichtenegger (JKU Linz)
   M.Efe Kinaci (JKU Linz)
------------------------------------------------------------------------- */

#ifdef FIX_CLASS

FixStyle(chem/shrink/core,FixChemShrinkCore)

#else

#ifndef LMP_FIX_CHEM_SHRINKCORE_H
#define LMP_FIX_CHEM_SHRINKCORE_H

#include "fix.h"
#include "fix_cfd_coupling.h"

namespace LAMMPS_NS {

class FixChemShrinkCore : public Fix  {

public:
  FixChemShrinkCore(class LAMMPS *, int, char **);
  ~FixChemShrinkCore();

  void post_create();
  void pre_delete(bool unfixflag);
  int setmask();

  virtual void updatePtrs();
  virtual void init();
  void init_defaults();
  virtual void post_force(int);

 protected:

  // functions declared in this class
  int active_layers(int);   // calculate number of active layers per-particle
  void calcMassLayer(int);  // calculate mass of layers per-particle
  void FractionalReduction(int); // calculate fractional reduction per-layer depending on layer radius
  void getXi(int, double *);    // calculate molar equilibrium constant of reacting gas
  double K_eq(int, double); // calculate equilibrium constant based on the work of Valipour 2009
  void getA(int);   // calculate chemical reaction resistance term
  void getB(int);   // calculate diffusion resistance term
  void getMassT(int);   // calculate gas film mass transfer resistance term
  void reaction(int, double *, double *);   // calculate chemical reaction rate
  void update_atom_properties(int, double *);   // update particle layers with depending on chemical reaction rate - per-particle
  void update_gas_properties(int, double *);    // update reactant and product gas masses depending on chemical reaction rate

  // variables
  int ts_create_, couple, ts;
  bool comm_established, screenflag_;
  double TimeStep;
  char* massA, *massC;
  double molMass_A_, molMass_C_, kch2_;
  char *diffA, *moleFrac;
  const int nmaxlayers_;    // maximum available layers - 3
  int layers_;          // current active layers
  const double rmin_;   // relative radius below which layers are neglected
  char *speciesA, *speciesC;

  // particle-layer variable values
  double **rhoeff_;
  double **porosity_;
  double pore_diameter_;
  double tortuosity_;
  double **relRadii_;
  double **massLayer_;
  const double *layerDensities_, *layerMolMasses_;
  const double *k0_, *Ea_;

  // particle propertis
  double *radius_;
  double *pmass_;
  double *pdensity_;

  // handles of fixes
  double *changeOfA_, *changeOfC_, *T_, *molecularDiffusion_, *nuf_, *Rep_, *X0_, *partP_, *Massterm; //*reactionHeat_,
  double **Aterm, **Bterm, **effDiffBinary, **effDiffKnud, **fracRed_;

  // coarse_graining factor
  double cg_;

  class FixPropertyAtom *fix_changeOfA_, *fix_changeOfC_;
  class FixPropertyAtom *fix_tgas_;
  // class FixPropertyAtom *fix_reactionHeat_;
  class FixPropertyAtom *fix_diffcoeff_;
  class FixPropertyAtom *fix_nuField_;
  class FixPropertyAtom *fix_partRe_;
  class FixPropertyAtom *fix_molefraction_;
  class FixPropertyAtom *fix_fracRed;
  class FixPropertyAtom *fix_Aterm;
  class FixPropertyAtom *fix_Bterm;
  class FixPropertyAtom *fix_Massterm;
  class FixPropertyAtom *fix_effDiffBinary;
  class FixPropertyAtom *fix_effDiffKnud;
  class FixPropertyAtom *fix_partPressure_;

  // particle properties
  class FixPropertyAtom *fix_layerRelRad_;
  class FixPropertyAtom *fix_layerMass_;
  class FixPropertyGlobal *fix_layerDens_;
  class FixPropertyGlobal *fix_layerMolMass_;
  class FixPropertyGlobal *fix_k0_;
  class FixPropertyGlobal *fix_Ea_;
  class FixPropertyAtom *fix_porosity_;
  class FixPropertyAtom *fix_rhoeff_;
  class FixPropertyGlobal *fix_tortuosity_;
  class FixPropertyGlobal *fix_pore_diameter_;
  class FixCfdCoupling* fc_;

};
}

#endif
#endif


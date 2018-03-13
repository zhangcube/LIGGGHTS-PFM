﻿/* ----------------------------------------------------------------------
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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "stdlib.h"
#include "atom.h"
#include "update.h"
#include "respa.h"
#include "error.h"
#include "memory.h"
#include "modify.h"
#include "comm.h"
#include "math.h"
#include "vector_liggghts.h"
#include "mpi_liggghts.h"
#include "fix_chem_shrink_core.h"
#include "fix_property_atom.h"
#include "pair_gran.h"
#include "compute_pair_gran_local.h"
#include "fix_property_global.h"
#include "properties.h"
#include "property_registry.h"
#include "global_properties.h"
#include "force.h"
#include "group.h"
#include "vector_liggghts.h"
#include "math_const.h"
#include <iostream>
#include <fstream>

using namespace LAMMPS_NS;
using namespace FixConst;
using namespace MathConst;

#define SMALL   1e-10
#define Runiv   8.3144

/* ---------------------------------------------------------------------- */

FixChemShrinkCore::FixChemShrinkCore(LAMMPS *lmp, int narg, char **arg) :
    Fix(lmp, narg, arg),
    nmaxlayers_(3),
    layers_(nmaxlayers_),
    rmin_(1e-8),       //  [m]
    fix_changeOfA_(0),
    fix_changeOfC_(0),
    fix_tgas_(0),
    fix_reactionHeat_(0),
    fix_diffcoeff_(0),
    fix_nuField_(0),
    fix_partRe_(0),
    fix_molefraction_(0),
    fix_fracRed(0),
    // for debug purposes, should be deleted afterwards
    fix_Aterm(0),
    fix_Bterm(0),
    fix_Massterm(0),
    fix_effDiffBinary(0),
    fix_effDiffKnud(0),
    fix_partPressure_(0),
    //
    fix_layerRelRad_(0),
    fix_layerMass_(0),      //  [kg]
    fix_dens_(0),           //  [kg/m^3]
    fix_molMass_(0),        //  [kg/mole]
    fix_k0_(0),             //  [m/s]
    fix_Ea_(0),             //  [J/mol] - [kg*m^2/s^2*mol]
    fix_porosity_(0),       //  [%]
    fix_rhoeff_(0),
    fix_tortuosity_(0),
    fix_pore_diameter_(0)   //  [m]
{
    if ((strncmp(style, "chem/shrink/core", 16) == 0) && ((!atom->radius_flag) || (!atom->rmass_flag)))
        error->all(FLERR, "Fix chem/shrink/core needs per particle radius and mass");

    // defaults
    screenflag_ =   0;
    molMass_A_  =   molMass_C_  =  kch2_ = 0.;
    diffA = moleFrac = NULL;
    // double **
    porosity_ = relRadii_ = massLayer_ = fracRed_ = rhoeff_ = NULL;
    Aterm = Bterm = effDiffBinary = effDiffKnud = NULL;
    // double *
    radius_ = pmass_ = pdensity_ = Massterm = NULL;
    changeOfA_ = changeOfC_ = T_ = reactionHeat_ = partP_ = NULL;
    molecularDiffusion_ = nuf_ = Rep_ = X0_ = NULL;
    // const double *
    pore_diameter_ = tortuosity_ = 0.0;
    layerDensities_ = layerMolMasses_ = k0_ = Ea_ = NULL;
    speciesA    =   speciesC    =   NULL;

    cg_ = 0.0;
    comm_established = false;
    iarg_ = 3;

    if (narg < 11)
        error->all(FLERR, "not enough arguments");

    bool hasargs = true;

    while (iarg_ < narg && hasargs)
    {
        if (strcmp(arg[iarg_], "speciesA") == 0)
        {
            if (narg < iarg_ + 2)
                error->fix_error(FLERR, this, "not enough arguments for 'speciesA'");
            speciesA = new char[strlen(arg[iarg_ + 1])];
            strcpy(speciesA, arg[iarg_ + 1]);
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_], "molMassA") == 0)
        {
            if (iarg_ + 2 > narg)
                error->fix_error(FLERR, this, "Wrong number of arguments");
            molMass_A_ = atof(arg[iarg_ + 1]);
            if (molMass_A_ < 0.0)
                error->fix_error(FLERR, this, "molar mass of A is not defined");
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_], "speciesC") == 0)
        {
            if (iarg_ + 2 > narg)
                error->fix_error(FLERR, this, "not enough arguments for 'speciesC'");
            speciesC = new char[strlen(arg[iarg_ + 1])];
            strcpy(speciesC, arg[iarg_ + 1]);
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_], "molMassC") == 0)
        {
            if (iarg_ + 2 > narg)
                error->fix_error(FLERR, this, "Wrong number of arguments");
            molMass_C_ = atof(arg[iarg_ + 1]);
            if (molMass_C_ < 0.0)
                error->fix_error(FLERR, this, "molar mass of C is not defined");
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_],"kch2") == 0)
        {
            if (iarg_ + 2 > narg)
                error->fix_error(FLERR, this, "Wrong number of arguments");
            kch2_ = atof(arg[iarg_ + 1]);
            if (kch2_ < 0.0)
                error->fix_error(FLERR, this, "carbon or hydrogen content is not defined");
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_],"screen") == 0)
        {
            if (iarg_+2 > narg) error->all(FLERR,"Illegal fix/chem/shrink command");
            if (strcmp(arg[iarg_+1],"yes") == 0) screenflag_ = 1;
            else if (strcmp(arg[iarg_+1],"no") == 0) screenflag_ = 0;
            else error->all(FLERR,"Illegal fix/chem/shrink command");
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_],"nevery") == 0)
        {
            nevery = atoi(arg[iarg_+1]);
            if (nevery <= 0) error->fix_error(FLERR,this,"");
            iarg_+=2;
            hasargs = true;
        }
        else if (strcmp(this->style,"chem/shrink") == 0)
        {
            error->fix_error(FLERR,this,"necessary keyword is missing");
        }
    }

    // define changed species mass A
    massA = new char [strlen("Modified_")+strlen(speciesA)+1];
    strcpy(massA,"Modified_");
    strcat(massA,speciesA);

    // define changed species mass C
    massC = new char [strlen("Modified_")+strlen(speciesC)+1];
    strcpy(massC,"Modified_");
    strcat(massC,speciesC);

    // define diffusant species
   diffA = new char [strlen(speciesA)+strlen("_diffCoeff")+1];
   strcpy(diffA,speciesA);
   strcat(diffA,"_diffCoeff");

    // reacting species bulk molar fraction
    moleFrac = new char [strlen("X_")+strlen(speciesA)+1];
    strcpy(moleFrac,"X_");
    strcat(moleFrac,speciesA);

    time_depend = 1;
    force_reneighbor = 1;
    next_reneighbor = update -> ntimestep + nevery;
    ts_create_  =   update->ntimestep;

    cg_ = force->cg();
}

/* ---------------------------------------------------------------------- */

FixChemShrinkCore::~FixChemShrinkCore()
{
    delete []massA;
    delete []massC;
    delete []diffA;
    delete []moleFrac;

    delete []speciesA;
    delete []speciesC;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::pre_delete(bool unfixflag)
{
    if (unfixflag)
    {
        if (fix_changeOfA_)     modify  ->  delete_fix(massA);
        if (fix_changeOfC_)     modify  ->  delete_fix(massC);
        if (fix_tgas_)          modify  ->  delete_fix("partTemp");
        if (fix_reactionHeat_)  modify  ->  delete_fix("reactionHeat");
        if (fix_diffcoeff_)     modify  ->  delete_fix(diffA);
        if (fix_nuField_)       modify  ->  delete_fix("partNu");
        if (fix_partRe_)        modify  ->  delete_fix("partRe");
        if (fix_molefraction_)  modify  ->  delete_fix(moleFrac);
        if (fix_fracRed)        modify  ->  delete_fix("fracRed");
        if (fix_Aterm)          modify  ->  delete_fix("Aterm");
        if (fix_Bterm)          modify  ->  delete_fix("Bterm");
        if (fix_Massterm)       modify  ->  delete_fix("Massterm");
        if (fix_effDiffBinary)  modify  ->  delete_fix("effDiffBinary");
        if (fix_effDiffKnud)    modify  ->  delete_fix("effDiffKnud");
        if (fix_partPressure_)  modify  ->  delete_fix("partP");
        if (fix_layerRelRad_)   modify  ->  delete_fix("relRadii");
        if (fix_layerMass_)     modify  ->  delete_fix("massLayer");
        if (fix_porosity_)      modify  ->  delete_fix("porosity_");
        if (fix_rhoeff_)        modify  ->  delete_fix("rhoeff_");
    }
}

/* ---------------------------------------------------------------------- */

int FixChemShrinkCore::setmask()
{
  int mask = 0;
  mask |= POST_FORCE;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::post_create()
{
    fix_layerRelRad_ =  static_cast<FixPropertyAtom*>(modify->find_fix_property("relRadii","property/atom","vector",0,0,this->style,false));
    if (!fix_layerRelRad_)
    {
        const char* fixarg[12];
        fixarg[0]="layerRelRad";        // fixid
        fixarg[1]="all";
        fixarg[2]="property/atom";
        fixarg[3]="relRadii";           // propertyid
        fixarg[4]="vector";
        fixarg[5]="yes";
        fixarg[6]="no";
        fixarg[7]="no";
        fixarg[8]="0.0";
        fixarg[9]="0.0";
        fixarg[10]="0.0";
        fixarg[11]="0.0";
        //modify->add_fix(12,const_cast<char**>(fixarg));
        fix_layerRelRad_ = modify->add_fix_property_atom(12,const_cast<char**>(fixarg),style);
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::updatePtrs()
{
    changeOfA_      =   fix_changeOfA_      ->  vector_atom;
    changeOfC_      =   fix_changeOfC_      ->  vector_atom;
    T_              =   fix_tgas_           ->  vector_atom;
    reactionHeat_   =   fix_reactionHeat_   ->  vector_atom;
    molecularDiffusion_  = fix_diffcoeff_   ->  vector_atom;
    nuf_            =   fix_nuField_        ->  vector_atom;
    Rep_            =   fix_partRe_         ->  vector_atom;
    X0_             =   fix_molefraction_   ->  vector_atom;
    relRadii_       =   fix_layerRelRad_    ->  array_atom;
    massLayer_      =   fix_layerMass_      ->  array_atom;
    layerDensities_ =   fix_dens_           ->  get_values();
    layerMolMasses_ =   fix_molMass_        ->  get_values();
    k0_             =   fix_k0_             ->  get_values();
    Ea_             =   fix_Ea_             ->  get_values();
    porosity_       =   fix_porosity_       ->  array_atom;
    rhoeff_         =   fix_rhoeff_         ->  array_atom;
    //
    tortuosity_     =   fix_tortuosity_     ->  compute_scalar();
    pore_diameter_  =   fix_pore_diameter_  ->  compute_scalar();
    //
    fracRed_        =   fix_fracRed         ->  array_atom;
    Aterm           =   fix_Aterm           ->  array_atom;
    Bterm           =   fix_Bterm           ->  array_atom;
    Massterm        =   fix_Massterm        ->  vector_atom;
    effDiffBinary   =   fix_effDiffBinary   ->  array_atom;
    effDiffKnud     =   fix_effDiffKnud     ->  array_atom;
    partP_          =   fix_partPressure_   ->  vector_atom;

    TimeStep        =   update  -> dt;
    radius_         =   atom    -> radius;
    pmass_          =   atom    -> rmass;
    pdensity_       =   atom    -> density;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::init()
{
    // error checks
    if (!atom->radius_flag)
      error->fix_error(FLERR,this,"requires atom attribute radius (per-particle)");
    if (!atom->rmass_flag)
      error->fix_error(FLERR,this,"requires atom attribute mass (per-particle)");
    if (!atom->tag_enable || 0 == atom->map_style)
      error->fix_error(FLERR,this,"requires atom tags and an atom map");

    // find coupling fix & get coupling interval value
    fc_ = static_cast<FixCfdCoupling*>(modify->find_fix_style_strict("couple/cfd",0));
    couple = fc_ -> couple_nevery_ + 1;

    // look up pre-exponential factor k0
    // differs for every ore id
    int ntype = atom -> ntypes;
    char* fixname = new char[strlen(id)+1];
    strcpy (fixname,"k0_");
    strcat(fixname,id);
    if (screenflag_ && screen)
        fprintf(screen,"fixname k0_: %s \n", fixname);
    fix_k0_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname,"property/global","vector",ntype,0,"FixChemShrinkCore"));

    // look up activation energies Ea
    // differs for every ore id
    strcpy(fixname, "Ea_");
    strcat(fixname, id);
    if (screenflag_ && screen)
        fprintf(screen,"fixname Ea_: %s \n", fixname);
    fix_Ea_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname, "property/global", "vector", ntype, 0, "FixChemShrinkCore"));
    delete[]fixname;

    // Layer Molar Mass
    fixname = new char [strlen("molMass_")+strlen(group->names[igroup])];
    strcpy(fixname,"molMass_");
    strcat(fixname,group->names[igroup]);
    if (screenflag_ && screen)
        fprintf(screen,"fixname molMass_: %s \n", fixname);
    fix_molMass_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname,"property/global","vector",ntype,0,"FixChemShrinkCore"));
    delete []fixname;

    // Layer Density
    fixname = new char [strlen("density_")+strlen(group->names[igroup])];
    strcpy(fixname,"density_");
    strcat(fixname,group->names[igroup]);
    if (screenflag_ && screen)
        fprintf(screen,"fixname density_: %s \n", fixname);
    fix_dens_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname,"property/global","vector",ntype,0,"FixChemShrinkCore"));
    delete []fixname;

    // references for per atom properties.
    fix_changeOfA_      =   static_cast<FixPropertyAtom*>(modify->find_fix_property(massA, "property/atom", "scalar", 0, 0, id));
    fix_changeOfC_      =   static_cast<FixPropertyAtom*>(modify->find_fix_property(massC, "property/atom", "scalar", 0, 0, id));
    fix_tgas_           =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partTemp", "property/atom", "scalar", 0, 0, style));
    fix_reactionHeat_   =   static_cast<FixPropertyAtom*>(modify->find_fix_property("reactionHeat", "property/atom", "scalar", 0, 0, id));
    fix_diffcoeff_      =   static_cast<FixPropertyAtom*>(modify->find_fix_property(diffA, "property/atom", "scalar", 0, 0, id));
    fix_nuField_        =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partNu", "property/atom", "scalar", 0, 0, style));
    fix_partRe_         =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partRe", "property/atom", "scalar", 0, 0, style));
    fix_molefraction_   =   static_cast<FixPropertyAtom*>(modify->find_fix_property(moleFrac, "property/atom", "scalar", 0, 0, id));
    fix_fracRed         =   static_cast<FixPropertyAtom*>(modify->find_fix_property("fracRed", "property/atom", "vector", 0, 0, style));
    fix_Aterm           =   static_cast<FixPropertyAtom*>(modify->find_fix_property("Aterm", "property/atom", "vector", 0, 0, style));
    fix_Bterm           =   static_cast<FixPropertyAtom*>(modify->find_fix_property("Bterm", "property/atom", "vector", 0, 0, style));
    fix_Massterm        =   static_cast<FixPropertyAtom*>(modify->find_fix_property("Massterm", "property/atom", "scalar", 0, 0, style));
    fix_effDiffBinary   =   static_cast<FixPropertyAtom*>(modify->find_fix_property("effDiffBinary", "property/atom", "vector", 0, 0, style));
    fix_effDiffKnud     =   static_cast<FixPropertyAtom*>(modify->find_fix_property("effDiffKnud", "property/atom", "vector", 0, 0, style));
    fix_partPressure_   =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partP", "property/atom", "scalar", 0, 0, id));

    fix_layerMass_      =   static_cast<FixPropertyAtom*>(modify->find_fix_property("massLayer","property/atom","vector",0,0,style,"FixChemShrinkCore"));

    fix_porosity_       =   static_cast<FixPropertyAtom*>(modify->find_fix_property("porosity_", "property/atom", "vector", 0, 0, "FixChemShrinkCore"));
    fix_rhoeff_         =   static_cast<FixPropertyAtom*>(modify->find_fix_property("rhoeff_", "property/atom", "vector", 0, 0, "FixChemShrinkCore"));
    // references for global properties - valid for every particle equally
    fix_tortuosity_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property("tortuosity_", "property/global", "scalar", 0, 0, style));
    fix_pore_diameter_ =   static_cast<FixPropertyGlobal*>(modify->find_fix_property("pore_diameter_", "property/global", "scalar", 0, 0,style));

    updatePtrs();

    // get initial values for rhoeff, and use them to calculate mass of layers
    for (int i = 0; i < atom->nlocal; ++i)
    {
        for (int layer=0; layer <= layers_; layer++)
        {
            rhoeff_[i][layer] = (1.0 - porosity_[i][layer])*layerDensities_[layer];

            if (screenflag_ && screen)
                fprintf(screen, "rhoeff_[0]: %f, rhoeff_[1]: %f, rhoeff_[2]: %f, rhoeff_[3]: %f \n",
                        rhoeff_[i][0],rhoeff_[i][1],rhoeff_[i][2],rhoeff_[i][3]);
        }

        calcMassLayer(i);
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::post_force(int)
{
    updatePtrs();
    int i = 0;
    int nlocal  =   atom->nlocal;
    int *mask   =   atom->mask;
    double x0_eq_[nmaxlayers_] = {};          // molar fraction of reactant gas
    double dmA_[nmaxlayers_] = {};            // mass flow rate of reactant gas species for each layer at w->fe, m->w & h->m interfaces
    ts = update->ntimestep;

    // do chem/shrink/core calculations if communication between CFDEM and LIGGGHTS already happened
    // need initial values from CFDEM side
    if (!comm_established)
    {
        if (ts > ts_create_ + couple)
        {
            comm_established = true;
        }
    }

    if (comm_established)
    {
        for (i = 0; i < nlocal; i++)
        {
            if (screenflag_ && screen)
            {
                fprintf(screen, "dCoeff value is : %.10f \n", molecularDiffusion_[i]);
            }

            if (mask[i] & groupbit)
            {
                // 1st recalculate masses of layers if layer has reduced
                // is ignored if there is no change in layers
                active_layers(i);
                // calculate values for fractional reduction f_i = (1-relRadii_i^3)
                // or with mass ratio - is irrelevant gives same result
                // provides simplicity for calculations of A & B terms.
                if (active_layers(i) > 0)
                {
                    FractionalReduction(i);
                    // get values for equilibrium molar fraction of reactant gas species,
                    // this value is calculated from the Equilibrium constants function Keq(layer,T).
                    // and used in the reaction rate determination.
                    getXi(i,x0_eq_);
                    // calculate the reaction resistance term
                    getA(i);
                    // calculate the diffusion resistance term
                    getB(i);
                    // calculate mass transfer resistance term
                    getMassT(i);
                    // do the reaction calculation with pre-calculated values of A, B and ß (massT)
                    // the USCM model chemical reaction rate with gaseous species model
                    // based on the works of Philbrook, Spitzer and Manning
                    reaction(i, dmA_, x0_eq_);
                    // the results of reaction gives us the mass change of reactant species gas
                    // in the usual case that means the CO gas mass species change is given
                    // this information is used then to calculate mass changes of particle layers
                    update_atom_properties(i, dmA_);
                    // also the results of reaction function is used to calculate
                    // the changes in gas species
                    update_gas_properties(i, dmA_);
                }
            }
        }
    }

    bigint nblocal = atom->nlocal;
    MPI_Allreduce(&nblocal,&atom->natoms,1,MPI_LMP_BIGINT,MPI_SUM,world);

    //NP tags and maps
    if (atom->molecular == 0) {
    int *tag = atom->tag;
    for (i = 0; i < atom->nlocal; i++) tag[i] = 0;
    atom->tag_extend();
    }

    if (atom->tag_enable) {
      if (atom->map_style) {
        atom->nghost = 0;
        atom->map_init();
        atom->map_set();
      }
    }
}

/* ---------------------------------------------------------------------- */

int FixChemShrinkCore::active_layers(int i)
{
    for(int j  = layers_; j >= 1; j--)
    {
        if (relRadii_[i][j]*(radius_[i]/cg_) < rmin_)
        {
            layers_ -= 1;
            calcMassLayer(i);
        }
    }
    if (screenflag_ && screen)
        fprintf(screen, "active layers: %i \n", layers_);
    return layers_;
}

/* ---------------------------------------------------------------------- */

// 0 = iron shell, 1 = wüstite layer, 2 = magnetite layer, 3 = hematite layer
void FixChemShrinkCore::calcMassLayer(int i)
{
    double rad[nmaxlayers_+1] = {};
    for (int layer = 0; layer <= layers_ ; layer++)
        rad[layer] = (radius_[i]/cg_)*relRadii_[i][layer];
    if (screenflag_ && screen)
    {
        fprintf(screen, "rad[0]: %f, rad[1]: %f, rad[2]: %f, rad[3]: %f \n",
                rad[0],rad[1],rad[2],rad[3]);
    }

    massLayer_[i][layers_]   =   1.33333*M_PI*rad[layers_]*rad[layers_]*rad[layers_]*rhoeff_[i][layers_];
    for (int layer = 0 ; layer < layers_; layer++)
    {
        massLayer_[i][layer]   =   1.33333*M_PI*(rad[layer]*rad[layer]*rad[layer]-rad[layer+1]*rad[layer+1]*rad[layer+1])*rhoeff_[i][layer];
    }

    if (screenflag_ && screen)
    {
        fprintf(screen, "CALC MASS: mass layer h: %6.15f ", massLayer_[i][3]);
        fprintf(screen, "mass layer m: %6.15f ", massLayer_[i][2]);
        fprintf(screen, "mass layer w: %6.15f ", massLayer_[i][1]);
        fprintf(screen, "mass layer fe: %6.15f \n", massLayer_[i][0]);
    }
}

/* ---------------------------------------------------------------------- */

double FixChemShrinkCore::K_eq(int layer, double T)
{
    // 0 = wustite , 1 = mangetite, 2 = hematite interfaces;
    double Keq_ = 0.;
     if (strcmp(speciesA, "CO") == 0)
    {
        if (layer == 0)
            Keq_ = exp(2744.63/T-2.946);
        else if (layer == 1)
            Keq_ = exp(-3585.64/T+8.98);
        else if (layer == 2)
            Keq_ = exp(3968.37/T+3.94);
     }
     else if(strcmp(speciesA,"H2")==0)
     {
         if (layer == 0)
             Keq_   =   exp(-1586.9/T + 0.9317);
         else if (layer == 1)
             Keq_   =   exp(-7916.6/T + 8.46);
         else if (layer == 2)
             Keq_   =   exp(-362.6/T + 10.334);
     }
     else
     {
         printf("Error : Undefined Reaction \n");
     }

     if(screenflag_ && screen)
         fprintf(screen,"Keq_ : %F \n",Keq_);

    return Keq_;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::getXi(int i, double *x0_eq_)
{
    for (int j = 0; j < layers_; j++)
    {
        x0_eq_[j]  =   kch2_/(1.0+K_eq(j,T_[i]));
    }
}

/* ---------------------------------------------------------------------- */

// calculate A_[j] [s/m] - the chemical reaction resistance term
// Equation available in literature. (Valipour, Natsui, Nietrost...)
// 0 = wüstite interface, 1 = magnetite interface, 2 = hematite interface
void FixChemShrinkCore::getA(int i)
{
    for (int j = 0; j < layers_ ; j++)
    {
        Aterm[i][j]   =   (k0_[j]*exp(-Ea_[j]/(Runiv*T_[i])))*cbrt((1.0-fracRed_[i][j])*(1.0-fracRed_[i][j]))*(1+1/K_eq(j,T_[i]));
        Aterm[i][j]   =   1.0/Aterm[i][j];
    }

    if (screenflag_ && screen)
        fprintf(screen, "Aterm_1: %f, Aterm_2: %f, Aterm_3: %f \n",Aterm[i][0], Aterm[i][1], Aterm[i][2]);

    // if hematite layer is reduced no chemical reaction is taking place at its surface
    if (layers_ == 2)
        Aterm[i][layers_] = 0.0;
    // if magnetite is reduced no chemical reaction is taking place at its surface
    if (layers_ == 1)
        Aterm[i][layers_] = 0.0;
}

/* ---------------------------------------------------------------------- */

// Calculate B [s/m] - the diffusion resistance term
// Use binary diffusion for mixture, and knudsen diffusion to determine the effective diffusion term
// 0 : diffusion through iron layer, 1 : diffusion through wüstite, 2 : diffusion through magnetite.
// there is no diffusion through the hematite layer
void FixChemShrinkCore::getB(int i)
{
    double fracRedThird_[nmaxlayers_] = {};
    double diffEff_[nmaxlayers_] = {};

    for (int layer = 0; layer < layers_; layer++)
    {
        // calculate fractional reduction to the power of 1/3 for simpler use
        fracRedThird_[layer] = cbrt(1.0-fracRed_[i][layer]);
        fracRedThird_[layer] = std::max(fracRedThird_[layer], SMALL);

        if (screenflag_ && screen)
            fprintf(screen, "tortuosity: %f, pore diameter: %f, porosity_1: %f, porosity_2: %f, porosity_3: %f \n"
                            "layer Mw_[0]: %f, layer Mw_[1]: %f, layer Mw_[2]: %f, layer Mw_[3]: %f \n"
                            "layer Dens_[0]: %f, layer Dens_[1]: %f, layer Dens_[2]: %f, layer Dens_[3]: %f \n",
                            tortuosity_,pore_diameter_,porosity_[i][0],porosity_[i][1],porosity_[i][2],
                            layerMolMasses_[0],layerMolMasses_[1],layerMolMasses_[2],layerMolMasses_[3],
                            layerDensities_[0],layerDensities_[1],layerDensities_[2],layerDensities_[3]);

        // Calculate the effective molecular diffusion
        effDiffBinary[i][layer] = molecularDiffusion_[i]*(porosity_[i][layer]/tortuosity_) + SMALL;

        if (screenflag_ && screen)
            fprintf(screen, "Dij_eff[0]: %f, Dij_eff[1]: %f, Dij_eff[2]: %f \n", effDiffBinary[i][0],effDiffBinary[i][1],effDiffBinary[i][2]);

        // Calculate the knudsen diffusion
        effDiffKnud[i][layer]  =  (pore_diameter_/6.0)*sqrt((8*Runiv*T_[i])/(M_PI*molMass_A_))*(porosity_[i][layer]/tortuosity_) + SMALL;

        if (screenflag_ && screen)
            fprintf(screen, "Dik_eff[0]: %f, Dik_eff[1]: %f, Dik_eff[2]: %f \n", effDiffKnud[i][0],effDiffKnud[i][1],effDiffKnud[i][2]);

        // total effective diffusivity
        diffEff_[layer] =   effDiffKnud[i][layer]*effDiffBinary[i][layer]/(effDiffBinary[i][layer]+effDiffKnud[i][layer]) + SMALL;
    }

    if (screenflag_ && screen)
    {
        fprintf(screen, "diffEff_[0]: %f, diffEff_[1]: %f, diffEff_[2]: %f \n fracRedThird_[0]: %f, fracRedThird_[1]: %f, fracRedThird_[2] : %f \n"
                        "fracRed_[0]: %f, fracRed_[1]: %f, fracRed_[2]: %f \n"
                ,diffEff_[0], diffEff_[1], diffEff_[2], fracRedThird_[0], fracRedThird_[1] , fracRedThird_[2],fracRed_[0][0], fracRed_[0][1], fracRed_[0][2]);
    }

    // calculation of diffusion term
    Bterm[i][0]   =   ((1-fracRedThird_[0])/fracRedThird_[0])*((radius_[i]/cg_)/diffEff_[0]);
    for (int layer = 1; layer <layers_; layer++)
    {
        Bterm[i][layer] = (fracRedThird_[layer-1]-fracRedThird_[layer])/(fracRedThird_[layer-1]*fracRedThird_[layer])*((radius_[i]/cg_)/diffEff_[layer]);
    }

    if (screenflag_ && screen)
    {
        fprintf(screen, "Bterm layer 1: %f ,",Bterm[i][0]);
        fprintf(screen, "Bterm layer 2: %f ,",Bterm[i][1]);
        fprintf(screen, "Bterm layer 3: %f \n",Bterm[i][2]);
    }

    // if hematite layer is reduced; no more diffusion in magnetite
    if (layers_ == 2)
        Bterm[i][layers_] = 0.0;
    // if magnetite is reduced; no more diffusion in wustite
    if (layers_ == 1)
        Bterm[i][layers_] = 0.0;
   /* for (int layer = 0; layer <= layers_; layer++)
    {
        Bterm[i][layer] = 0.0;
    } */
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::getMassT(int i)
{
    // initialize sherwood & schmidt numbers for every particle
    double Sc_[atom->nlocal] = {};
    double Sh_[atom->nlocal] = {};

    // if molecular diffusion is around 0, overwrite to avoid numerical errors.
    if (molecularDiffusion_[i] < SMALL)
        Sc_[i] = SMALL;
    else
        Sc_[i]  =   nuf_[i]/molecularDiffusion_[i];

    Sh_[i]  =   2.0+0.6*sqrt(Rep_[i])*cbrt(Sc_[i]);

    Massterm[i] = Sh_[i]*molecularDiffusion_[i]/(2.0*(radius_[i]/cg_)) + SMALL;
    Massterm[i] = 1.0/Massterm[i];

    if (screenflag_ && screen)
        fprintf(screen, "Schmidt number: %f, molecularDiffusion: %6.15f, NuField: %6.15f \n",Sc_[i],molecularDiffusion_[i],nuf_[i]);
   // Massterm[i] = 0.0;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::reaction(int i, double *dmA_, double *x0_eq_)
{
    updatePtrs();
    double W = 0.;
    double dY[nmaxlayers_] = {};

    if (layers_ == nmaxlayers_)
    {
        // including reaction resistance and diffusion coeff terms
        W = (Aterm[i][2]+Bterm[i][2])*(Aterm[i][0]*(Aterm[i][1]+Bterm[i][1]+Bterm[i][0]+Massterm[i])+(Aterm[i][1]+Bterm[i][1])*(Bterm[i][0]+Massterm[i]))
                +Aterm[i][1]*(Aterm[i][0]*(Bterm[i][1]+Bterm[i][0]+Massterm[i])+Bterm[i][1]*(Bterm[i][0]+Massterm[i]));
        // hematite to magnetite
        dY[2]   =   ((Aterm[i][0]*(Aterm[i][1]+Bterm[i][1]+Bterm[i][0]+Massterm[i])+(Bterm[i][0]+Massterm[i])*(Aterm[i][1]+Bterm[i][1]))*(X0_[i]-x0_eq_[2])
                -   (Aterm[i][0]*(Bterm[i][1]+Bterm[i][0]+Massterm[i])+Bterm[i][1]*(Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[1])
                -   (Aterm[i][1]*(Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[0]))/W;
        // magnetite to wustite
        dY[1]   =   (((Aterm[i][2]+Bterm[i][2]+Bterm[i][1])*(Aterm[i][0]+Bterm[i][0]+Massterm[i])+Aterm[i][0]*(Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[1])
                -   (Bterm[i][1]*(Aterm[i][0]+Bterm[i][0]+Massterm[i])+Aterm[i][0]*(Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[2])
                -   ((Aterm[i][2]+Bterm[i][2])*(Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[0]))/W;
        // wustite to iron
        dY[0]   =   (((Aterm[i][2]+Bterm[i][2])*(Aterm[i][1]+Bterm[i][1]+Bterm[i][0]+Massterm[i])+Aterm[i][1]*(Bterm[i][1]+Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[0])
                -   (Aterm[i][1]*(Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[2])
                -   ((Aterm[i][2]+Bterm[i][2])*(Bterm[i][0]+Massterm[i]))*(X0_[i]-x0_eq_[1]))/W;
    }
    else if (layers_ == 2)
    {
        W = (Aterm[i][1]+Bterm[i][1])*(Aterm[i][0]+Bterm[i][0]+Massterm[i])+Aterm[i][0]*(Bterm[i][0]+Massterm[i]);

        // hematite to magnetite
        dY[2]   =   0.0;
        // magnetite to wustite
        dY[1]   =   ((Aterm[i][0]+Bterm[i][0]+Massterm[i])*(X0_[i]-x0_eq_[1])-(Bterm[i][0]+Massterm[i])*(X0_[i]-x0_eq_[0]))/W;
        // wustite to iron
        dY[0]   =   ((Aterm[i][1]+Bterm[i][1]+Bterm[i][0]+Massterm[i])*(X0_[i]-x0_eq_[0])-(Bterm[i][0]+Massterm[i])*(X0_[i]-x0_eq_[1]))/W;
    }
    else if (layers_ == 1)
    {
        // rate of chemical reaction for 1 active layer
        W = Aterm[i][0]+Bterm[i][0]+Massterm[i];

        // hematite to magnetite
        dY[2]   =   0.0;
        // magnetite to wustite
        dY[1]   =   0.0;
        // wustite to iron
        dY[0]   =   (X0_[i] - x0_eq_[0])/W;
    }

    if (screenflag_ && screen)
        fprintf(screen, "pressure: %f , T_[i]: %f, Runiv: %f, molMass_A_: %f \n", partP_[i],T_[i],Runiv,molMass_A_);

    for (int j = 0 ; j < layers_; j++)
    {
        // mass flow rate for reactant gas species
        dmA_[j] =   dY[j]*partP_[i]*(1.0/(Runiv*T_[i]))*molMass_A_*(4.0*M_PI*((radius_[i]*radius_[i])/(cg_*cg_)))*TimeStep*nevery;
    }

    if (screenflag_ && screen)
        fprintf(screen,"dm_gas[0]: %6.15f, dm_gas[1]: %6.15f, dm_gas[2]: %6.15f \n", dmA_[0], dmA_[1], dmA_[2]);
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::update_atom_properties(int i, double *dmA_)
{
    if (screenflag_ && screen)
        fprintf(screen,"run update atom props \n");
    // based on material change: update relative radii, average density and mass of atom i
    // stoichiometric coefficients of reactions
    double nu_reac_[3] = {1, 1, 3};
    double nu_prod_[3] = {1, 3, 2};
    // particle stuff
    double rad[nmaxlayers_+1] = {};
    double dmL_[nmaxlayers_+1] = {};     // mass flow rate between each layer i.e. (btw h->m, m->w, w->Fe) must consider reduction and growth at the same time
    double sum_mass_new = 0.0;           // just for control case --- delete afterwards

     if(screenflag_ && screen)
    {
        fprintf(screen, "massLayer_h = %6.15f ,", massLayer_[i][3]);
        fprintf(screen, "massLayer_m = %6.15f ,", massLayer_[i][2]);
        fprintf(screen, "massLayer_w = %6.15f ,", massLayer_[i][1]);
        fprintf(screen, "massLayer_fe = %6.15f \n", massLayer_[i][0]);
    }

    // calculate mass flow rate of dmL[layer]
    // Keep in mind the stoichiometric coefficients of reaction prod - reactants
    dmL_[0] = -dmA_[0]*nu_prod_[0]*(layerMolMasses_[0]/molMass_A_);
    for (int layer = 1; layer < layers_; layer++)
        dmL_[layer] = -dmA_[layer]*nu_prod_[layer]*(layerMolMasses_[layer]/molMass_A_) + dmA_[layer-1]*nu_reac_[layer-1]*(layerMolMasses_[layer]/molMass_A_);
    dmL_[layers_] = dmA_[layers_-1]*nu_reac_[layers_-1]*(layerMolMasses_[layers_]/molMass_A_);

    if (layers_ == 2)
        dmL_[layers_+1] = 0.0;
    else if (layers_ == 1)
    {
        dmL_[layers_+2] = 0.0;
        dmL_[layers_+1] = 0.0;
    }

    if (screenflag_ && screen)
    {
        fprintf(screen, "dmL hematite: %6.15f ,", dmL_[3]);
        fprintf(screen, "dmL magnetite: %6.15f ,", dmL_[2]);
        fprintf(screen, "dmL Wüstite: %6.15f ,", dmL_[1]);
        fprintf(screen, "dmL Iron: %6.15f \n", dmL_[0]);
    }

    // get new layer masses and Volumes
    for (int j = 0; j <= layers_; j++)
    {
        massLayer_[i][j] -= dmL_[j];
        massLayer_[i][j] = std::max(massLayer_[i][j], SMALL);

        // calculate total mass of particle
        sum_mass_new    +=  massLayer_[i][j];
    }

    // update the layer radius with the new massLayer
    if (screenflag_ && screen)
            fprintf(screen, "rhoeff_h: %f, rhoeff_m: %f, rhoeff_w: %f, rhoeff_Fe: %f \n",rhoeff_[0][3],rhoeff_[0][2],rhoeff_[0][1],rhoeff_[0][0]);

    rad[layers_] = cbrt((0.75*massLayer_[i][layers_])/(rhoeff_[i][layers_]*M_PI));

    for (int layer = layers_ - 1; layer >= 0; layer--)
        rad[layer]   =   cbrt((0.75*massLayer_[i][layer]/(rhoeff_[i][layer]*M_PI))+rad[layer+1]*rad[layer+1]*rad[layer+1]);

    //detemine the new relative layer radii and store them in relRadii_
    for (int j = 0; j <= layers_; j++)
    {
        relRadii_[i][j] = rad[j]/rad[0];
    }

    radius_[i] = rad[0]*cg_;

    if (screenflag_ && screen)
            fprintf(screen, "UPDATE ATOM: radi[3]: %f, radi[2]: %f, radi[1]; %f, radi[0]: %f \n relRadii[3]: %f, relRadii[2]: %f, relRadii[1]: %f, relRadii[0]: %f \n",
                    rad[3], rad[2], rad[1], rad[0], relRadii_[0][3],relRadii_[0][2],relRadii_[0][1],relRadii_[0][0]);

    // new total mass
    pmass_[i]   =   sum_mass_new*cg_*cg_*cg_;

    // total particle effective density
    pdensity_[i]    =   0.75*pmass_[i]/(M_PI*radius_[i]*radius_[i]*radius_[i]);

    if(screenflag_ && screen)
        fprintf(screen, "radius_: %f, pmass_: %f, pdensity_: %f\n ", radius_[i], pmass_[i], pdensity_[i]);

}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::update_gas_properties(int i, double *dmA_)
{
   // based on material change: update gas-phase source terms for mass and heat
    for (int j = 0; j < nmaxlayers_;j++)
    {
        changeOfA_[i]   -=  dmA_[j];
        changeOfC_[i]   +=  dmA_[j]*molMass_C_/molMass_A_;
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::FractionalReduction(int i)
{
    // calculate the fractional reduction as defined in Tang et al. (2012)
    // "Simulation study on performance of Z-path Moving-fluidized Bed for Gaseous Reduction
    // of Iron Ore Fines" ISIJ International, Vol. 52, No. 7, pp. 1241 - 1249
    /* double f_[nmaxlayers_];
    for (int layer=0; layer<layers_; layer++)
         f_[layer] = 0.0; */

    /* f_[0] = 1 - ((2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1])/(2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1]+massLayer_[i][0]/layerMolMasses_[0]));
    f_[1] = 1 - ((2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2])/(2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1]+massLayer_[i][0]/layerMolMasses_[0]));
    f_[2] = 1 - ((2*massLayer_[i][3]/layerMolMasses_[3])/(2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1]+massLayer_[i][0]/layerMolMasses_[0])); */

    // for testing purposes if radius changes with fix type
    for (int layer=0; layer<layers_; layer++)
    {
        //fracRed_[i][layer] = f_[layer];
        fracRed_[i][layer] = 1.0 - relRadii_[i][layer+1]*relRadii_[i][layer+1]*relRadii_[i][layer+1];
        fracRed_[i][layer] = std::max(fracRed_[i][layer], SMALL);
    }

    if (screenflag_ && screen)
            fprintf(screen, "Fn before update fracRed[0]: %f, fracRed[1]: %f, fracRed[2] : %f \n"
                            "Fn before update relRadii[0]: %f, relRadii[1]: %f, relRadii[2]: %f, relRadii: [3]:%f \n",
                    fracRed_[0][0], fracRed_[0][1], fracRed_[0][2], relRadii_[0][0], relRadii_[0][1],relRadii_[0][2],relRadii_[0][3]);
}

/* ---------------------------------------------------------------------- */

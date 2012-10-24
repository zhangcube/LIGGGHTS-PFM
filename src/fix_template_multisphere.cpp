/* ----------------------------------------------------------------------
LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
Transfer Simulations
www.liggghts.com | www.cfdem.com
Christoph Kloss, christoph.kloss@cfdem.com

LIGGGHTS is based on LAMMPS
LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
http://lammps.sandia.gov, Sandia National Laboratories
Steve Plimpton, sjplimp@sandia.gov

Copyright (2003) Sandia Corporation. Under the terms of Contract
DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
certain rights in this software. This software is distributed under
the GNU General Public License.

See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
Thanks to Chris Stoltz (P&G) for providing
a Fortran version of the MC integrator
------------------------------------------------------------------------- */

#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "fix_template_multisphere.h"
#include "math_extra.h"
#include "math_extra_liggghts.h"
#include "vector_liggghts.h"
#include "atom.h"
#include "atom_vec.h"
#include "domain.h"
#include "update.h"
#include "respa.h"
#include "modify.h"
#include "group.h"
#include "comm.h"
#include "force.h"
#include "output.h"
#include "fix_multisphere.h"
#include "memory.h"
#include "error.h"
#include "random_mars.h"
#include "random_park.h"
#include "fix_rigid.h"
#include "particleToInsert_multisphere.h"
#include "input_multisphere.h"

using namespace LAMMPS_NS;
using namespace LMP_PROBABILITY_NS;
using namespace FixConst;

#define LARGE 1e8
#define EPSILON 1.0e-7
#define TOLERANCE 1.0e-6

#define MIN(A,B) ((A) < (B)) ? (A) : (B)
#define MAX(A,B) ((A) > (B)) ? (A) : (B)

#define MAXJACOBI 50
#define BIG 1.e20

/*NL*/#define LMP_DEBUGMODE_MULTISPHERE false

/* ---------------------------------------------------------------------- */

FixTemplateMultisphere::FixTemplateMultisphere(LAMMPS *lmp, int narg, char **arg) :
  FixTemplateMultiplespheres(lmp, narg, arg)
{
    delete pti;
    pti = new ParticleToInsertMultisphere(lmp,nspheres);

    memory->create(displace_,nspheres,3,"FixTemplateMultiplespheres:moi_");

    volumeweight_ = new double[nspheres];

    type_ = 0;

    // parse args
    // should have the possibility to parse inertia tensor here
    // TODO: should parse cross section area matrix here

    bool hasargs = true;
    while (iarg < narg && hasargs)
    {
        hasargs = false;

        if (strcmp(arg[iarg],"type") == 0)
        {
            hasargs = true;
            iarg++;
            type_ = atoi(arg[iarg++]);
        }
        else if(strcmp(style,"particletemplate/multisphere") == 0)
            error->fix_error(FLERR,this,"unknown keyword");
    }

    // check if type has been defined
    if(type_ < 1)
        error->fix_error(FLERR,this,"have to provide a type >=1");
}

/* ---------------------------------------------------------------------- */

void FixTemplateMultisphere::post_create()
{
  // bounding sphere, center of mass calculations
  FixTemplateMultiplespheres::post_create();

  // calculate inertia_ tensor and its eigensystem
  calc_inertia();
  calc_eigensystem();

  print_info();
}

/* ---------------------------------------------------------------------- */

void FixTemplateMultisphere::init()
{
    // check consistency of type as defined in each command of this style
    // types must be from 1..ntemplates
    // is fulfilled if min and max is reached

    FixTemplateMultisphere *ftms_i,*ftms_j;

    int type_i;
    int type_min = 10000, type_max = 0;
    int n_fixes = modify->n_fixes_style_strict(style);

    for(int i = 0; i < n_fixes; i++)
    {
        ftms_i = static_cast<FixTemplateMultisphere*>(modify->find_fix_style_strict(style,i));
        type_i = ftms_i->type();

        if(type_i < type_min) type_min = type_i;
        if(type_i > type_max) type_max = type_i;

        // check if any other uses the same type
        for(int j = i+1; j < n_fixes; j++)
        {
            ftms_j = static_cast<FixTemplateMultisphere*>(modify->find_fix_style_strict(style,j));
            if(ftms_j != ftms_i && type_i == ftms_j->type())
                error->fix_error(FLERR,this,"multisphere template types have to be unique");
        }
    }

    // types are consecutive if no double usage and min/max is met
    if(type_min != 1 || type_max != n_fixes)
        error->fix_error(FLERR,this,"multisphere template types have to be consecutive starting from 1");
}

/* ----------------------------------------------------------------------
   calc volume weight for each sphere
   necessary for volume fraction on CFD side
------------------------------------------------------------------------- */

void FixTemplateMultisphere::calc_volumeweight()
{
    double x_try[3],distSqr,n_hits;

    int *hits_j = new int[nspheres];
    int *hits_only_j = new int[nspheres];

    vectorZeroizeN(hits_j,nspheres);
    vectorZeroizeN(hits_only_j,nspheres);

    // volumeweight_ = 0.5 + 0.5 * hits only in j / hits in j

    for(int i = 0; i < ntry; i++)
    {
        generate_xtry(x_try);
        n_hits = 0;

        for(int j = 0; j < nspheres; j++)
        {
            distSqr = dist_sqr(j,x_try);
            if(distSqr < r_sphere[j]*r_sphere[j])
            {
                hits_j[j]++;
                n_hits++;
            }
        }

        if(n_hits == 1)
        {
            for(int j = 0; j < nspheres; j++)
            {
                distSqr = dist_sqr(j,x_try);
                if(distSqr < r_sphere[j]*r_sphere[j])
                {
                    hits_only_j[j]++;
                    n_hits++;
                }
            }
        }
    }

    // calculate volume weight
    for(int j = 0; j < nspheres; j++)
        volumeweight_[j] = 0.5 + 0.5 * hits_only_j[j]/hits_j[j];

    delete []hits_j;
    delete []hits_only_j;
}

/* ----------------------------------------------------------------------
   calc inertia_ tensor
------------------------------------------------------------------------- */

void FixTemplateMultisphere::calc_inertia()
{
  double x_try[3],xcm[3],distSqr;
  int n_found = 0;
  bool alreadyChecked;

  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"MC integration for inertia_ tensor\n");
  for(int i = 0; i < 3; i++)
    vectorZeroize3D(moi_[i]);

  for(int i = 0; i < ntry; i++)
  {
      generate_xtry(x_try);

      alreadyChecked = false;
      for(int j = 0; j < nspheres; j++)
      {
          if (alreadyChecked) break;
          distSqr = dist_sqr(j,x_try);
          if(distSqr < r_sphere[j]*r_sphere[j])
          {
              moi_[0][0] +=  (x_try[1]-xcm[1])*(x_try[1]-xcm[1]) + (x_try[2]-xcm[2])*(x_try[2]-xcm[2]);
              moi_[0][1] -=  (x_try[0]-xcm[0])*(x_try[1]-xcm[1]);
              moi_[0][2] -=  (x_try[0]-xcm[0])*(x_try[2]-xcm[2]);
              moi_[1][0] -=  (x_try[1]-xcm[1])*(x_try[0]-xcm[0]);
              moi_[1][1] +=  (x_try[0]-xcm[0])*(x_try[0]-xcm[0]) + (x_try[2]-xcm[2])*(x_try[2]-xcm[2]);
              moi_[1][2] -=  (x_try[1]-xcm[1])*(x_try[2]-xcm[2]);
              moi_[2][0] -=  (x_try[2]-xcm[2])*(x_try[0]-xcm[0]);
              moi_[2][1] -=  (x_try[2]-xcm[2])*(x_try[1]-xcm[1]);
              moi_[2][2] +=  (x_try[0]-xcm[0])*(x_try[0]-xcm[0]) + (x_try[1]-xcm[1])*(x_try[1]-xcm[1]);
              alreadyChecked = true;
          }
      }
  }
  for(int i = 0; i < 3; i++)
      for(int j = 0; j < 3; j++)
          moi_[i][j] *= expectancy(pdf_density)/static_cast<double>(ntry)*(x_max[0]-x_min[0])*(x_max[1]-x_min[1])*(x_max[2]-x_min[2]);

  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"MC integration done\n");
  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen," moi_=%1.10f|%1.10f|%1.10f\n",moi_[0][0],moi_[0][1],moi_[0][2]);
  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen," moi_=%1.10f|%1.10f|%1.10f\n",moi_[1][0],moi_[1][1],moi_[1][2]);
  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen," moi_=%1.10f|%1.10f|%1.10f\n",moi_[2][0],moi_[2][1],moi_[2][2]);

  // check if tensor is symmetric enough(numerical errors)
  // not sure if this check is sufficient
  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"Checking tensor symmetry\n");
  if(fabs(moi_[0][1]/moi_[1][0]-1.) > TOLERANCE) error->all(FLERR,"Fix particletemplate/multisphere:Error when calculating inertia_ tensor : Not enough accuracy. Boost ntry.");
  if(fabs(moi_[0][2]/moi_[2][0]-1.) > TOLERANCE) error->all(FLERR,"Fix particletemplate/multisphere:Error when calculating inertia_ tensor : Not enough accuracy. Boost ntry.");
  if(fabs(moi_[2][1]/moi_[1][2]-1.) > TOLERANCE) error->all(FLERR,"Fix particletemplate/multisphere:Error when calculating inertia_ tensor : Not enough accuracy. Boost ntry.");

  // make the tensor symmetric
  /*NL*/if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"Check OK, forcing symmetry\n");
  moi_[0][1] = (moi_[0][1]+moi_[1][0])/2.;
  moi_[1][0] = moi_[0][1];
  moi_[0][2] = (moi_[0][2]+moi_[2][0])/2.;
  moi_[2][0] = moi_[0][2];
  moi_[2][1] = (moi_[2][1]+moi_[2][0])/2.;
  moi_[1][2] = moi_[2][1];

}

/* ----------------------------------------------------------------------
   calc eigensystem of inertia_ tensor
------------------------------------------------------------------------- */

void FixTemplateMultisphere::calc_eigensystem()
{
  // following operations as in fix_rigid.cpp::init()

  //-----------------------------------------
  // calculate eigenvalues and eigenvectors
  //-----------------------------------------

  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"Calculating eigenvalues and eigenvectors\n");
  double evectors[3][3];
  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"Performing jacobi calc\n");

  int ierror = MathExtra::jacobi(moi_,static_cast<double*>(inertia_),evectors);
  if (ierror) error->fix_error(FLERR,this,"Insufficient Jacobi rotations for rigid body");

  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"Jacobi calc finished\n");
  ex_space_[0] = evectors[0][0];
  ex_space_[1] = evectors[1][0];
  ex_space_[2] = evectors[2][0];
  ey_space_[0] = evectors[0][1];
  ey_space_[1] = evectors[1][1];
  ey_space_[2] = evectors[2][1];
  ez_space_[0] = evectors[0][2];
  ez_space_[1] = evectors[1][2];
  ez_space_[2] = evectors[2][2];

  // if any principal moment < scaled EPSILON, set to 0.0
  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"Removing unnecessary intertia terms\n");
  double max;
  max = MAX(inertia_[0],inertia_[1]);
  max = MAX(max,inertia_[2]);

  if (inertia_[0] < EPSILON*max) inertia_[0] = 0.0;
  if (inertia_[1] < EPSILON*max) inertia_[1] = 0.0;
  if (inertia_[2] < EPSILON*max) inertia_[2] = 0.0;

  // enforce 3 evectors as a right-handed coordinate system
  // flip 3rd evector if needed

  double ez[3];
  vectorCross3D(ex_space_,ey_space_,ez);
  double dot = vectorDot3D(ez,ez_space_);
  if (dot < 0.) vectorScalarMult3D(ez_space_,-1.);

  //NP test for valid principal moments & axes like in fix_rigid.cpp init() omitted here

  // calculate displace_
  double del[3];
  for(int i = 0; i < nspheres; i++)
  {
      /*NP old version:
      displace_[i][0] = del[0]*ex_space_[0] + del[1]*ex_space_[1] + del[2]*ex_space_[2];
      displace_[i][1] = del[0]*ey_space_[0] + del[1]*ey_space_[1] + del[2]*ey_space_[2];
      displace_[i][2] = del[0]*ez_space_[0] + del[1]*ez_space_[1] + del[2]*ez_space_[2];*/

      // can do it like this because ex, ey, ez are orthogonal, using xcm = 0/0/0
      MathExtraLiggghts::cartesian_coosys_to_local_orthogonal(displace_[i],x_sphere[i], ex_space_, ey_space_, ez_space_,error);
      /*NL*///fprintf(screen,"displace_ for particle %d: %f %f %f\n",i,displace_[i][0],displace_[i][1],displace_[i][2]);
  }

  // transform in body coodinates, could do this with
  // solve Mt*xcm_to_xb_body = xcm_to_xb (where xcm_to_xb == x_bound because xcm is 0 0 0)
  MathExtraLiggghts::cartesian_coosys_to_local_orthogonal(xcm_to_xb_body_,x_bound,ex_space_,ey_space_,ez_space_,error);

  /*NL*/ if(LMP_DEBUGMODE_MULTISPHERE) fprintf(screen,"Calculated eigenvectors and eigenvalues\n");
}

/* ---------------------------------------------------------------------- */

void FixTemplateMultisphere::print_info()
{
  if (logfile)
  {
    fprintf(logfile,"Finished calculating properties of template\n");
    fprintf(logfile,"   mass = %e, radius of bounding sphere = %e, radius of equivalent sphere = %e\n",mass_expect,r_bound,r_equiv);
    fprintf(logfile,"   center of mass = %e, %e, %e\n",0.,0.,0.);
    fprintf(logfile,"   Principal moments of inertia_: %e, %e, %e\n",inertia_[0],inertia_[1],inertia_[2]);
    fprintf(logfile,"     Eigenvector: %e, %e, %e\n",ex_space_[0],ex_space_[1],ex_space_[2]);
    fprintf(logfile,"     Eigenvector: %e, %e, %e\n",ey_space_[0],ey_space_[1],ey_space_[2]);
    fprintf(logfile,"     Eigenvector: %e, %e, %e\n",ez_space_[0],ez_space_[1],ez_space_[2]);
  }
}

/* ---------------------------------------------------------------------- */

FixTemplateMultisphere::~FixTemplateMultisphere()
{
    memory->destroy(displace_);
    delete []volumeweight_;

    delete pti;
    if(pti_list) delete_ptilist();
}

/* ----------------------------------------------------------------------*/

void FixTemplateMultisphere::randomize_single()
{
  //NP displace_, ex,ey,ez are for reference orientation

  pti->nspheres = nspheres;
  pti->density_ins = expectancy(pdf_density);
  pti->volume_ins = volume_expect;
  pti->mass_ins = mass_expect;
  pti->r_bound_ins = r_bound;
  pti->atom_type = atom_type;

  ParticleToInsertMultisphere *pti_m = static_cast<ParticleToInsertMultisphere*>(pti);

  for(int j = 0; j < nspheres; j++)
  {
      pti_m->radius_ins[j] = r_sphere[j];
      vectorCopy3D(x_sphere[j],pti_m->x_ins[j]);
      vectorCopy3D(displace_[j],pti_m->displace[j]);
  }

  vectorCopy3D(inertia_,pti_m->inertia);
  vectorCopy3D(ex_space_,pti_m->ex_space);
  vectorCopy3D(ey_space_,pti_m->ey_space);
  vectorCopy3D(ez_space_,pti_m->ez_space);
  vectorCopy3D(xcm_to_xb_body_,pti_m->xcm_to_xbound);

  vectorZeroize3D(pti_m->xcm_ins);
  quatUnitize4D(pti_m->quat_ins);
  vectorZeroize3D(pti_m->v_ins);
  vectorZeroize3D(pti_m->omega_ins);

  pti->groupbit = groupbit;

  /*NL*///double test[3];
  /*NL*///fprintf(screen,"transformed %e %e %e into %e %e %e\n",x_bound[0],x_bound[1],x_bound[2],xcm_to_xb_body[0],xcm_to_xb_body[1],xcm_to_xb_body[2]);
  /*NL*///fprintf(screen,"ex ey ez is  %f %f %f , %f %f %f, %f %f %f\n",ex_space_[0],ex_space_[1],ex_space_[2],ey_space_[0],ey_space_[1],ey_space_[2],ez_space_[0],ez_space_[1],ez_space_[2]);

   //NP nothing to do here
   //NP variable density and diameter not implemented

   //NP rotate ex_space_, ey_space_, ez_space_ done later
   //pti->random_rotate(random->uniform(),random->uniform(),random->uniform());
}

/* ----------------------------------------------------------------------*/

void FixTemplateMultisphere::init_ptilist(int n_random_max)
{
    n_pti_max = n_random_max;
    pti_list = (ParticleToInsert**) memory->smalloc(n_pti_max*sizeof(ParticleToInsert*),"pti_list");

    for(int i = 0; i < n_pti_max; i++)
       pti_list[i] = new ParticleToInsertMultisphere(lmp,nspheres);

    /*NL*/ //fprintf(screen,"init_list called with n_pti_max %d\n",n_pti_max);
}

/* ----------------------------------------------------------------------*/

void FixTemplateMultisphere::delete_ptilist()
{
    /*NL*/ //fprintf(screen,"delete_list called with n_pti_max %d\n",n_pti_max);
    if(n_pti_max == 0) return;

    for(int i = 0; i < n_pti_max; i++)
       delete pti_list[i];

    memory->sfree(pti_list);
    pti_list = NULL;
    n_pti_max = 0;
}

/* ----------------------------------------------------------------------*/

void FixTemplateMultisphere::randomize_ptilist(int n_random,int distribution_groupbit)
{
    //NP variable density and diameter not implemented

    for(int i = 0; i < n_random; i++)
    {
          //NP displace_, ex,ey,ez are for reference orientation

          ParticleToInsertMultisphere *pti_m = static_cast<ParticleToInsertMultisphere*>(pti_list[i]);

          pti_m->nspheres = nspheres;
          pti_m->density_ins = expectancy(pdf_density);
          pti_m->volume_ins = volume_expect;
          pti_m->mass_ins = mass_expect;
          pti_m->r_bound_ins = r_bound;
          pti_m->atom_type = atom_type;

          for(int j = 0; j < nspheres; j++)
          {
              pti_m->radius_ins[j] = r_sphere[j];
              vectorCopy3D(x_sphere[j],pti_m->x_ins[j]);
              vectorCopy3D(displace_[j],pti_m->displace[j]);
          }

          vectorCopy3D(inertia_,pti_m->inertia);
          vectorCopy3D(ex_space_,pti_m->ex_space);
          vectorCopy3D(ey_space_,pti_m->ey_space);
          vectorCopy3D(ez_space_,pti_m->ez_space);
          vectorCopy3D(xcm_to_xb_body_,pti_m->xcm_to_xbound);

          vectorZeroize3D(pti_m->xcm_ins);
          quatUnitize4D(pti_m->quat_ins);
          vectorZeroize3D(pti_m->v_ins);
          vectorZeroize3D(pti_m->omega_ins);

          pti_m->groupbit = groupbit | distribution_groupbit; //NP also contains insert_groupbit
    }
}

/* ---------------------------------------------------------------------- */

void FixTemplateMultisphere::finalize_insertion()
{
    if(modify->n_fixes_style("multisphere") != 1)
        error->fix_error(FLERR,this,"Multi-sphere particle inserted: You have to use exactly one fix multisphere");

    static_cast<FixMultisphere*>(modify->find_fix_style("multisphere",0))->add_body_finalize();
}

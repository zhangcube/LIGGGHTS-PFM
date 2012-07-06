/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations

   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com

   This file was modified with respect to the release in LAMMPS
   Modifications are Copyright 2009-2012 JKU Linz
                     Copyright 2012-     DCS Computing GmbH, Linz

   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing authors for original version: Leo Silbert (SNL), Gary Grest (SNL)
------------------------------------------------------------------------- */

#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "fix_wall_gran_hooke_history.h"
#include "pair_gran_hooke_history.h"
#include "atom.h"
#include "force.h"
#include "update.h"
#include "pair.h"
#include "modify.h"
#include "memory.h"
#include "error.h"
#include "fix_property_global.h"
#include "compute_pair_gran_local.h"
#include "fix_property_atom.h"
#include "mech_param_gran.h"
#include "fix_rigid.h"
#include "vector_liggghts.h"
#include "fix_mesh.h"
#include "container.h"

using namespace LAMMPS_NS;
using namespace FixConst;

#define MIN(A,B) ((A) < (B)) ? (A) : (B)
#define MAX(A,B) ((A) > (B)) ? (A) : (B)

#define SMALL 1e-12

/*NL*/ #define DEBUGMODE_WALL_HOOKE false
/*NL*/ #define DEBUG_OUTP_WALL_HOOKE screen

/* ---------------------------------------------------------------------- */

FixWallGranHookeHistory::FixWallGranHookeHistory(LAMMPS *lmp, int narg, char **arg) :
  FixWallGran(lmp, narg, arg)
{
    // parse wall models

    // set defaults
    Temp_wall = -1.;
    Q = Q_add = 0.;
    dampflag = 1;
    cohesionflag = 0;
    rollingflag = 0;

    bool hasargs = true;
    while(iarg_ < narg && hasargs)
    {
        hasargs = false;
        if (strcmp(arg[iarg_],"cohesion") == 0) {
            iarg_++;
            if(strcmp(arg[iarg_],"sjkr") == 0)
                cohesionflag = 1;
            else if(strcmp(arg[iarg_],"off") == 0)
                cohesionflag = 0;
            else
                error->fix_error(FLERR,this,"expecting 'sjkr' or 'off' after keyword 'cohesion'");
            iarg_++;
            hasargs = true;
        } else if (strcmp(arg[iarg_],"rolling_friction") == 0) {
            iarg_++;
            if(strcmp(arg[iarg_],"cdt") == 0)
                rollingflag = 1;
            else if(strcmp(arg[iarg_],"off") == 0)
                rollingflag = 0;
            else
                error->fix_error(FLERR,this,"expecting 'cdt' or 'off' after keyword 'rolling_friction'");
            iarg_++;
            hasargs = true;
        } else if (strcmp(arg[iarg_],"tangential_damping") == 0) {
            iarg_++;
            if(strcmp(arg[iarg_],"on") == 0)
                dampflag = 1;
            else if(strcmp(arg[iarg_],"off") == 0)
                dampflag = 0;
            else
                error->fix_error(FLERR,this,"expecting 'on' or 'off' after keyword 'dampflag'");
            iarg_++;
            hasargs = true;
        } else if (strcmp(arg[iarg_],"temperature") == 0) {
            if(is_mesh_wall())
                error->fix_error(FLERR,this,"for mesh walls temperature has to be defined for each mesh via fix mesh");
            iarg_++;
            Temp_wall = atof(arg[iarg_++]);
            hasargs = true;
        }
    }

    if (cohesionflag < 0 || cohesionflag > 1 || dampflag < 0 || dampflag > 3)
      error->fix_error(FLERR,this,"");
}

/* ---------------------------------------------------------------------- */

FixWallGranHookeHistory::~FixWallGranHookeHistory()
{

}

/* ---------------------------------------------------------------------- */

void FixWallGranHookeHistory::post_create()
{
    FixWallGran::post_create();
}

/* ---------------------------------------------------------------------- */

void FixWallGranHookeHistory::init_granular()
{
  //get material properties
  Yeff = ((PairGranHookeHistory*)pairgran_)->Yeff;
  Geff = ((PairGranHookeHistory*)pairgran_)->Geff;
  betaeff = ((PairGranHookeHistory*)pairgran_)->betaeff;
  veff = ((PairGranHookeHistory*)pairgran_)->veff;
  cohEnergyDens = ((PairGranHookeHistory*)pairgran_)->cohEnergyDens;
  coeffRestLog = ((PairGranHookeHistory*)pairgran_)->coeffRestLog;
  coeffFrict = ((PairGranHookeHistory*)pairgran_)->coeffFrict;
  coeffRollFrict = ((PairGranHookeHistory*)pairgran_)->coeffRollFrict;
  charVel = ((PairGranHookeHistory*)pairgran_)->charVel;

  //need to check properties for rolling friction and cohesion energy density here
  //since these models may not be active in the pair style
  //NP yet we write into the memory of the pair style
  int max_type = pairgran_->mpg->max_type();
  FixPropertyGlobal *coeffRollFrict1, *cohEnergyDens1;
  if(rollingflag)
    coeffRollFrict1=static_cast<FixPropertyGlobal*>(modify->find_fix_property("coefficientRollingFriction","property/global","peratomtypepair",max_type,max_type,style));
  if(cohesionflag)
    cohEnergyDens1=static_cast<FixPropertyGlobal*>(modify->find_fix_property("cohesionEnergyDensity","property/global","peratomtypepair",max_type,max_type,style));

  //pre-calculate parameters for possible contact material combinations
  for(int i=1;i< max_type+1; i++)
  {
      for(int j=1;j<max_type+1;j++)
      {
          if(rollingflag) coeffRollFrict[i][j] = coeffRollFrict1->compute_array(i-1,j-1);
          if(cohesionflag) cohEnergyDens[i][j] = cohEnergyDens1->compute_array(i-1,j-1);
      }
  }

  if(cohesionflag) error->warning(FLERR,"Cohesion model should only be used with hertzian contact laws.");
}

/* ---------------------------------------------------------------------- */

void FixWallGranHookeHistory::init_heattransfer()
{
    fppa_T = NULL;
    fppa_hf = NULL;
    deltan_ratio = NULL;

    if (!is_mesh_wall() && Temp_wall < 0.) return;
    else if (is_mesh_wall())
    {
        int heatflag = 0;
        for(int imesh = 0; imesh < n_meshes(); imesh++)
        {
            heatflag = heatflag || mesh_list()[imesh]->mesh()->prop().getGlobalProperty<ScalarContainer<double> >("Temp") != NULL;
        }

        if(!heatflag) return;
    }

    // set flag so addHeatFlux function is called
    heattransfer_flag_ = true;

    // if(screen && comm->me == 0) fprintf(screen,"Initializing wall/gran heat transfer model\n");
    fppa_T = static_cast<FixPropertyAtom*>(modify->find_fix_property("Temp","property/atom","scalar",1,0,style));
    fppa_hf = static_cast<FixPropertyAtom*>(modify->find_fix_property("heatFlux","property/atom","scalar",1,0,style));

    th_cond = static_cast<FixPropertyGlobal*>(modify->find_fix_property("thermalConductivity","property/global","peratomtype",0,0,style))->get_values();

    // if youngsModulusOriginal defined, get deltan_ratio
    Fix* ymo_fix = modify->find_fix_property("youngsModulusOriginal","property/global","peratomtype",0,0,style,false);
    // deltan_ratio is defined by heat transfer fix, see if there is one
    int n_htf = modify->n_fixes_style("heat/gran");

    // get deltan_ratio set by the heat transfer fix
    if(ymo_fix && n_htf) deltan_ratio = static_cast<FixPropertyGlobal*>(ymo_fix)->get_array_modified();
}

/* ---------------------------------------------------------------------- */

void FixWallGranHookeHistory::compute_force(int ip, double deltan, double rsq,double meff_wall, double dx, double dy, double dz,double *vwall,double *c_history, double area_ratio)
{
  double r,vr1,vr2,vr3,vnnr,vn1,vn2,vn3,vt1,vt2,vt3,wrmag;
  double wr1,wr2,wr3,damp,ccel,vtr1,vtr2,vtr3,vrel;
  double fn,fs,fs1,fs2,fs3,fx,fy,fz,tor1,tor2,tor3,r_torque[3],r_torque_n[3];
  double shrmag,rsht,rinv,rsqinv;
  double kn, kt, gamman, gammat, xmu, rmu;
  double cri, crj;

  double *f = atom->f[ip];
  double *torque = atom->torque[ip];
  double *v = atom->v[ip];
  double *omega = atom->omega[ip];
  double radius = atom->radius[ip];
  double mass = atom->rmass[ip];
  double cr = radius - 0.5*deltan;

  if(fix_rigid_ && body_[ip] >= 0)
    mass = masstotal_[body_[ip]];

  /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_compute_force 0\n");
  /*NL*///printVec3D(screen,"shear history",c_history);

  //get the parameters needed to resolve the contact
  deriveContactModelParams(ip,deltan,meff_wall,kn,kt,gamman,gammat,xmu,rmu);

  r = sqrt(rsq);
  rinv = 1.0/r;
  rsqinv = 1.0/rsq;

  // relative translational velocity

  vr1 = v[0] - vwall[0];
  vr2 = v[1] - vwall[1];
  vr3 = v[2] - vwall[2];

  // normal component

  vnnr = vr1*dx + vr2*dy + vr3*dz;
  vn1 = dx*vnnr * rsqinv;
  vn2 = dy*vnnr * rsqinv;
  vn3 = dz*vnnr * rsqinv;

  // tangential component

  vt1 = vr1 - vn1;
  vt2 = vr2 - vn2;
  vt3 = vr3 - vn3;

  // relative rotational velocity
  // in case of wall contact, r is the contact radius

  wr1 = cr*omega[0] * rinv;
  wr2 = cr*omega[1] * rinv;
  wr3 = cr*omega[2] * rinv;

  /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_compute_force 1\n");

  // normal forces = Hookian contact + normal velocity damping

  damp = gamman*vnnr*rsqinv;       //NP modified C.K.
  ccel = kn*(radius-r)*rinv - damp;
  /*NL*/ //ccel = MAX(0.,ccel);//NP modified C.K. remove artificial force effect (Poeschel, Schwager: Comp. Gran. Dynamics)

  if(cohesionflag)
  {
      double Fn_coh;
      addCohesionForce(ip, r, Fn_coh,area_ratio);
      ccel-=Fn_coh*rinv;
  }

  /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_CF_0 normal force %f %f %f\n",dx*ccel,dy*ccel,dz*ccel);
  /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_CF_0 damp %f, vwall %f %f %f \n",damp,vwall[0],vwall[1],vwall[2]);

  // relative velocities

  vtr1 = vt1 - (dz*wr2-dy*wr3);
  vtr2 = vt2 - (dx*wr3-dz*wr1);
  vtr3 = vt3 - (dy*wr1-dx*wr2);
  vrel = vtr1*vtr1 + vtr2*vtr2 + vtr3*vtr3;
  vrel = sqrt(vrel);

  /*NL*///if(atom->tag[ip] == 2044)fprintf(screen,"step %d: vrel %f, vp %f %f %f vw %f %f %f\n",update->ntimestep,vrel,v[0],v[1],v[2],vwall[0],vwall[1],vwall[2]);

  // shear history effects
  if (shearupdate_ && addflag_)
  {
      c_history[0] += vtr1*dt_;
      c_history[1] += vtr2*dt_;
      c_history[2] += vtr3*dt_;

      // rotate shear displacements
      rsht = c_history[0]*dx + c_history[1]*dy + c_history[2]*dz;
      rsht = rsht*rsqinv;
      c_history[0] -= rsht*dx;
      c_history[1] -= rsht*dy;
      c_history[2] -= rsht*dz;
  }

  shrmag = sqrt(c_history[0]*c_history[0] + c_history[1]*c_history[1] + c_history[2]*c_history[2]);

   /*NP this is the old way of doing it
  tangential forces = shear + tangential velocity damping

  fs1 = - (kt*c_history[0] + gammat*vtr1);  //NP modified C.K.
  fs2 = - (kt*c_history[1] + gammat*vtr2);  //NP modified C.K.
  fs3 = - (kt*c_history[2] + gammat*vtr3);  //NP modified C.K.

  rescale frictional displacements and forces if needed

  fs = sqrt(fs1*fs1 + fs2*fs2 + fs3*fs3);
  fn = xmu * fabs(ccel*r);

  if (fs > fn) {
    if (shrmag != 0.0) {
      c_history[0] = (fn/fs) * (c_history[0] + gammat*vtr1/kt) - gammat*vtr1/kt;     //NP modified C.K.

      c_history[1] = (fn/fs) * (c_history[1] + gammat*vtr2/kt) - gammat*vtr2/kt;     //NP modified C.K.

      c_history[2] = (fn/fs) * (c_history[2] + gammat*vtr3/kt) - gammat*vtr3/kt;     //NP modified C.K.

      fs1 *= fn/fs ;
      fs2 *= fn/fs;
      fs3 *= fn/fs;
    } else fs1 = fs2 = fs3 = 0.0;
  }*/

  // tangential forces = shear + tangential velocity damping

  fs1 = - (kt*c_history[0]);
  fs2 = - (kt*c_history[1]);
  fs3 = - (kt*c_history[2]);

  /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_CF_1 shear forces before rescale %f %f %f\n",fs1,fs2,fs3);

  // rescale frictional displacements and forces if needed

  fs = sqrt(fs1*fs1 + fs2*fs2 + fs3*fs3);
  fn = xmu * fabs(ccel*r);

  // energy loss from sliding or damping
  if (fs > fn) {
      if (shrmag != 0.0) {
          fs1 *= fn/fs;
          fs2 *= fn/fs;
          fs3 *= fn/fs;
          c_history[0]=-fs1/kt;
          c_history[1]=-fs2/kt;
          c_history[2]=-fs3/kt;
      }
      else fs1 = fs2 = fs3 = 0.0;
  }
  else
  {
      fs1 -= (gammat*vtr1);
      fs2 -= (gammat*vtr2);
      fs3 -= (gammat*vtr3);
  }

  /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_CF_2 shear forces after rescale %f %f %f\n",fs1,fs2,fs3);

  // forces & torques

  fx = dx*ccel + fs1;
  fy = dy*ccel + fs2;
  fz = dz*ccel + fs3;

  if(addflag_)
  {
      f[0] += fx*area_ratio;
      f[1] += fy*area_ratio;
      f[2] += fz*area_ratio;
  }

  /*NL*///if(atom->tag[ip] == 3)fprintf(screen,"step %d: force %e %e %e, delta %f %f %f, area_ratio %f\n",update->ntimestep,fx*area_ratio,fy*area_ratio,fz*area_ratio,dx,dy,dz,area_ratio);

  tor1 = rinv * (dy*fs3 - dz*fs2);
  tor2 = rinv * (dz*fs1 - dx*fs3);
  tor3 = rinv * (dx*fs2 - dy*fs1);

  // add rolling friction torque
  vectorZeroize3D(r_torque);
  if(rollingflag)
  {
            wrmag = sqrt(wr1*wr1+wr2*wr2+wr3*wr3);
            if (wrmag > 0.)
            {
                r_torque[0] = rmu*kn*(radius-r)*wr1/wrmag*cr;
            r_torque[1] = rmu*kn*(radius-r)*wr2/wrmag*cr;
            r_torque[2] = rmu*kn*(radius-r)*wr3/wrmag*cr;

            // remove normal (torsion) part of torque
            double rtorque_dot_delta = r_torque[0]*dx+ r_torque[1]*dy + r_torque[2]*dz;
            r_torque_n[0] = dx * rtorque_dot_delta * rsqinv;
            r_torque_n[1] = dy * rtorque_dot_delta * rsqinv;
            r_torque_n[2] = dz * rtorque_dot_delta * rsqinv;
            vectorSubtract3D(r_torque,r_torque_n,r_torque);
            }
  }

  if(addflag_)
  {
      torque[0] -= cr*tor1*area_ratio + r_torque[0];
      torque[1] -= cr*tor2*area_ratio + r_torque[1];
      torque[2] -= cr*tor3*area_ratio + r_torque[2];
  }
  else if(cwl_)
    cwl_->add_wall_2(ip,fx,fy,fz,tor1*area_ratio,tor2*area_ratio,tor3*area_ratio,c_history,rsq);
}

/* ---------------------------------------------------------------------- */

//NP modified C.K.
void FixWallGranHookeHistory::addHeatFlux(TriMesh *mesh,int ip, double rsq, double area_ratio)
{
    //r is the distance between the sphere center and wall
    double tcop, tcowall, hc, Acont, delta_n, r;
    double reff_wall = atom->radius[ip];
    int itype = atom->type[ip];
    double ri = atom->radius[ip];

    if(mesh)
        Temp_wall = (*mesh->prop().getGlobalProperty< ScalarContainer<double> >("Temp"))(0);

    r = sqrt(rsq);

    //NP adjust overlap that may be superficially large due to softening
    if(deltan_ratio)
    {
       delta_n = ri - r;
       delta_n *= deltan_ratio[itype-1][atom_type_wall_-1];
       r = ri - delta_n;
    }

    Acont = (reff_wall*reff_wall-r*r)*M_PI*area_ratio; //contact area sphere-wall
    tcop = th_cond[itype-1]; //types start at 1, array at 0
    tcowall = th_cond[atom_type_wall_-1];

    if ((fabs(tcop) < SMALL) || (fabs(tcowall) < SMALL)) hc = 0.;
    else hc = 4.*tcop*tcowall/(tcop+tcowall)*sqrt(Acont);

    if(addflag_)
    {
        heatflux[ip] += (Temp_wall-Temp_p[ip]) * hc;
        Q_add += (Temp_wall-Temp_p[ip]) * hc * update->dt;
    }
    else if(cwl_)
        cwl_->add_heat_wall(ip,(Temp_wall-Temp_p[ip]) * hc);
    /*NL*/ //fprintf(screen,"adding heat flux of %f to particle %d\n",(T_wall-Temp_p[ip]) * hc,ip);
}

inline void FixWallGranHookeHistory::addCohesionForce(int &ip, double &r, double &Fn_coh,double area_ratio)
{
    //r is the distance between the sphere center and wall
    double reff_wall = atom->radius[ip];
    double Acont = (reff_wall*reff_wall-r*r)*M_PI; //contact area sphere-wall
    int itype = atom->type[ip];
    Fn_coh=cohEnergyDens[itype][atom_type_wall_]*Acont*area_ratio;
}

/* ---------------------------------------------------------------------- */

//NP modified C.K.
inline void FixWallGranHookeHistory::deriveContactModelParams(int ip, double deltan,double meff_wall, double &kn, double &kt, double &gamman, double &gammat, double &xmu,double &rmu) //NP modified C.K.
{
    /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_deriveContactModelParams 0\n");
    /*NP char *testmsg=new char[200];
    sprintf(testmsg,"Y=%f, v=%f, cr=%f, cf=%f\n",Y->compute_vector(0),v->compute_vector(0),coeffRest->compute_array(0,0),coeffFrict->compute_array(0,0));
    error->warning(testmsg);
    */

    /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_deriveContactModelParams 1\n");

    double sqrtval = sqrt(atom->radius[ip]);
    int itype = atom->type[ip];

    kn=16./15.*sqrtval*Yeff[itype][atom_type_wall_]*pow(15.*meff_wall*charVel*charVel/(16.*sqrtval*Yeff[itype][atom_type_wall_]),0.2);
    kt=kn;

    /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_deriveContactModelParams 10\n");

    gamman=sqrt(4.*meff_wall*kn/(1.+(M_PI/coeffRestLog[itype][atom_type_wall_])*(M_PI/coeffRestLog[itype][atom_type_wall_])));
    gammat=gamman;

    /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_deriveContactModelParams 11\n");

    xmu=coeffFrict[itype][atom_type_wall_];
    if(rollingflag)rmu=coeffRollFrict[itype][atom_type_wall_];

    /*NL*/ if(DEBUGMODE_WALL_HOOKE) fprintf(lmp->DEBUG_OUTP_WALL_HOOKE, "WALL_GRAN_deriveContactModelParams 2\n");

    if (dampflag == 0) gammat = 0.0;

    /*NP sprintf(testmsg,"ip=%d, meff=%f, deltan=%f, kn=%f, kt=%f, gamman=%f, gammat=%f\n", ip,meff,deltan,kn,kt,gamman,gammat);
    error->warning(testmsg);
    delete []testmsg;
    */
    return;
}

/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations

   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com

   Department for Particule Flow Modelling
   Copyright 2016- JKU Linz

   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   This software is distributed under the GNU General Public License.

   See the README file in the top-level directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing author:
   Daniel Queteschiner <daniel.queteschiner@jku.at> (JKU Linz)
------------------------------------------------------------------------- */
#ifdef FIX_CLASS

FixStyle(forcecontrol/region,FixForceControlRegion)

#else

#ifndef LMP_FIX_FORCECONTROL_REGION_H
#define LMP_FIX_FORCECONTROL_REGION_H

#include "fix.h"
#include <map>
#include <set>
#include <vector>

namespace LAMMPS_NS {

class FixForceControlRegion : public Fix {
 public:
  FixForceControlRegion(class LAMMPS *, int, char **);
  ~FixForceControlRegion();
  void post_create();
  int setmask();
  void init();
  void setup(int);
  void min_setup(int);
  void post_force(int);
  void reset_dt();
  double compute_scalar();
  double compute_vector(int);
  int modify_param(int narg, char **arg);

 private:
  double *xvalue;
  double *yvalue;
  double *zvalue;
  double foriginal[4],foriginal_all[4];
  int force_flag;

  double axis_[3];
  double vel_max_[3];
  double vel_min_[3];
  double ctrl_op_[3];
  double sp_vec_[3]; // set point
  double pv_vec_[3]; // process value
  double err_[3];
  double kp_,ki_,kd_;

  int ctrl_style_;
  // timesteps
  double dtf_,dtv_,dtv_inverse_;
  double fadex_,fadey_,fadez_;

  class FixAveEulerRegion *actual_;
  class FixAveEulerRegion *target_;
  double cg_, cg3_, cg_ratio_;
  int ncells_max_;
  double **old_pv_vec_;
  double **sum_err_;
  double const_part_;
  double sinesq_part_;
  double used_part_;
  std::set<int> active_;
  std::vector<bool> modifier_;
  std::map<class FixScaleDiameter*, std::set<int> > modifier_scale_;
  const double acceptable_deviation_min;
  const double acceptable_deviation_max;
  bool limit_velocity_;
  double limit[3];
};

}

#endif
#endif

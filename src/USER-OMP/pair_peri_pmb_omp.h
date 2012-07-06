/* -*- c++ -*- ----------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing author: Axel Kohlmeyer (Temple U)
------------------------------------------------------------------------- */

#ifdef PAIR_CLASS

PairStyle(peri/pmb/omp,PairPeriPMBOMP)

#else

#ifndef LMP_PAIR_PERI_PMB_OMP_H
#define LMP_PAIR_PERI_PMB_OMP_H

#include "pair_peri_pmb.h"
#include "thr_omp.h"

namespace LAMMPS_NS {

class PairPeriPMBOMP : public PairPeriPMB, public ThrOMP {

 public:
  PairPeriPMBOMP(class LAMMPS *);

  virtual void compute(int, int);
  virtual double memory_usage();

 private:
  template <int EVFLAG, int EFLAG, int NEWTON_PAIR>
  void eval(int ifrom, int ito, ThrData * const thr);
};

}

#endif
#endif

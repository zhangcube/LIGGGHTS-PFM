/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations

   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com

   Christoph Kloss, christoph.kloss@cfdem.com
   Copyright 2009-2012 JKU Linz
   Copyright 2012-     DCS Computing GmbH, Linz

   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   This software is distributed under the GNU General Public License.

   See the README file in the top-level directory.
------------------------------------------------------------------------- */

#ifdef CFD_DATACOUPLING_CLASS

   CfdDataCouplingStyle(one2one,CfdDatacouplingOne2One)

#else

#ifndef LMP_CFD_DATACOUPLING_ONE2ONE_H
#define LMP_CFD_DATACOUPLING_ONE2ONE_H

//#define O2O_DEBUG

#include "cfd_datacoupling.h"
#include "multisphere_parallel.h"
#include "error.h"
#include "properties.h"
#include <mpi.h>
#include <iostream>

namespace LAMMPS_NS {

class CfdDatacouplingOne2One : public CfdDatacoupling {
 public:
  CfdDatacouplingOne2One(class LAMMPS *, int,int, char **,class FixCfdCoupling*);
  ~CfdDatacouplingOne2One();

  void exchange();

  virtual void pull(const char *name, const char *type, void *&ptr, const char *datatype);
  virtual void push(const char *name, const char *type, void *&ptr, const char *datatype);

  template <typename T> void pull_mpi(const char *,const char *,void *&,const int*,const int);
  template <typename T> void push_mpi(const char *,const char *,void *&);

  virtual bool error_push()
  { return false;}

 private:
  template <typename T> MPI_Datatype mpi_type_dc();
};

/* ---------------------------------------------------------------------- */
//NP OF to LIGGGHTS, global to local
//NP ptr to is local, ptr from is global

template <typename T>
void CfdDatacouplingOne2One::pull_mpi
(
    const char *name,
    const char *type,
    void *&from,
    const int* ids,
    const int ncollected
)
{
    int len1 = -1, len2 = -1;
    // get reference where to write the data
    void * to = find_pull_property(name,type,len1,len2);

    if (atom->nlocal && (!to || len1 < 0 || len2 < 0))
    {
        if(screen) fprintf(screen,"LIGGGHTS could not find property %s to write data from calling program to.\n",name);
        lmp->error->one(FLERR,"This is fatal");
    }

    // return if no data to transmit
    if(len1*len2 < 1) return;

    T *from_t = (T*) from;
    if(strcmp(type,"scalar-atom") == 0)
    {
        #ifdef O2O_DEBUG
        std::cout << "["<<comm->me << "] scl " << name
                  << " ncollected: " << ncollected
                  << " len1 " <<  len1
                  << " len2 " <<  len2
                  << std::endl;
        #endif

        T *to_t = (T*) to;
        for (int i = 0; i < ncollected; i++)
        {
            int m = atom->map(ids[i]);
            if (m >= 0)
                to_t[m] = from_t[i];
        }
    }
    else if(strcmp(type,"vector-atom") == 0)
    {
        #ifdef O2O_DEBUG
        std::cout << "["<<comm->me << "] vec " << name
                  << " ncollected: " << ncollected
                  << " len1 " <<  len1
                  << " len2 " <<  len2
                  << std::endl;
        #endif
        T **to_t = (T**) to;
        for (int i = 0; i < ncollected; i++)
        {
            int m = atom->map(ids[i]);
            if (m >= 0)
            {
        #ifdef O2O_DEBUG
        std::cout << "["<<comm->me << "] vec " << name
                  << " i: " << i
                  << " id: " << ids[i]
                  << " m: " << m
                  << std::endl;
        #endif
                for (int j = 0; j < len2; j++)
                {

                    to_t[m][j] = from_t[i*len2+j];
                }
            }
        }
    }

/*
    // check memory allocation
    T* allred = check_grow<T>(len1*len2);

    // perform allreduce on incoming data - well, let's not
    T **from_t = (T**)from;
    MPI_Allreduce
    (
        &(from_t[0][0]),
        &(allred[0]),
        len1*len2,
        mpi_type_dc<T>(),
        MPI_SUM,
        comm
    );

    // copy data - loops over max # global atoms, bodies
    if(strcmp(type,"scalar-atom") == 0)
    {
        T *to_t = (T*) to;
        for (int i = 0; i < len1; i++)
            if ((m = atom->map(i+1)) >= 0)
                to_t[m] = allred[i];
    }
    else if(strcmp(type,"vector-atom") == 0)
    {
        T **to_t = (T**) to;
        for (int i = 0; i < len1; i++)
            if ((m = atom->map(i+1)) >= 0)
                for (int j = 0; j < len2; j++)
                    to_t[m][j] = allred[i*len2 + j];
    }
    else if(strcmp(type,"scalar-multisphere") == 0)
    {
        T *to_t = (T*) to;
        MultisphereParallel *ms_data = properties_->ms_data();
        if(!ms_data)
            error->one(FLERR,"Transferring a multisphere property from/to LIGGGHTS requires a fix multisphere");
        for (int i = 0; i < len1; i++)
            if ((m = ms_data->map(i+1)) >= 0)
                to_t[m] = allred[i];
    }
    else if(strcmp(type,"vector-multisphere") == 0)
    {
        T **to_t = (T**) to;
        MultisphereParallel *ms_data = properties_->ms_data();
        if(!ms_data)
            error->one(FLERR,"Transferring a multisphere property from/to LIGGGHTS requires a fix multisphere");
        for (int i = 0; i < len1; i++)
            if ((m = ms_data->map(i+1)) >= 0)
                for (int j = 0; j < len2; j++)
                    to_t[m][j] = allred[i*len2 + j];
    }
    else if(strcmp(type,"scalar-global") == 0 || strcmp(type,"vector-global") == 0 || strcmp(type,"matrix-global") == 0)
    {
        T **to_t = (T**) to;
        for (int i = 0; i < len1; i++)
            for (int j = 0; j < len2; j++)
                to_t[i][j] = allred[i*len2 + j];
    }
*/
    else error->one(FLERR,"Illegal data type in CfdDatacouplingOne2One::pull");
}

/* ---------------------------------------------------------------------- */
//NP LIGGGHTS to OF, local to global
//NP ptr from is local, ptr to is global
//NP len1 is global # of datums (max tag)

template <typename T>
void CfdDatacouplingOne2One::push_mpi
(
    const char *name,
    const char *type,
    void *&to
)
{
    error->one(FLERR,"Do not use one2one with twoWayMPI. ");
}

/* ---------------------------------------------------------------------- */

template<typename T>
inline MPI_Datatype CfdDatacouplingOne2One::mpi_type_dc()
{
    error->all(FLERR,"Illegal call to mpi_type_dc(), valid types are int and double");
    return 0;
}

template<>
inline MPI_Datatype CfdDatacouplingOne2One::mpi_type_dc<double>()
{
    return MPI_DOUBLE;
}

template<>
inline MPI_Datatype CfdDatacouplingOne2One::mpi_type_dc<int>()
{
    return MPI_INT;
}

/* ---------------------------------------------------------------------- */

}

#endif
#endif

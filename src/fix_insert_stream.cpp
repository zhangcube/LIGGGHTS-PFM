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

#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "fix_insert_stream.h"
#include "fix_mesh_surface.h"
#include "atom.h"
#include "atom_vec.h"
#include "force.h"
#include "update.h"
#include "comm.h"
#include "modify.h"
#include "vector_liggghts.h"
#include "domain.h"
#include "random_park.h"
#include "memory.h"
#include "error.h"
#include "fix_property_atom.h"
#include "fix_particledistribution_discrete.h"
#include "fix_rigid_multisphere.h"
#include "multisphere.h"
#include "fix_template_sphere.h"
#include "particleToInsert.h"

enum{FACE_NONE,FACE_MESH,FACE_CIRCLE};

using namespace LAMMPS_NS;
using namespace FixConst;

/*NL*/ #define LMP_DEBUGMODE_FIXINSERT_STREAM false
/*NL*/ #define LMP_DEBUG_OUT_FIXINSERT_STREAM screen

/* ---------------------------------------------------------------------- */

FixInsertStream::FixInsertStream(LAMMPS *lmp, int narg, char **arg) :
  FixInsert(lmp, narg, arg)
{
  // set defaults first, then parse args
  init_defaults();

  bool hasargs = true;
  while(iarg < narg && hasargs)
  {
    hasargs = false;
    if (strcmp(arg[iarg],"insertion_face") == 0)
    {
      //NP should be possible to define either mesh or face
      if (iarg+2 > narg) error->fix_error(FLERR,this,"");
      int f_i = modify->find_fix(arg[iarg+1]);
      if (f_i == -1) error->fix_error(FLERR,this,"Could not find fix mesh/gran id you provided for the fix insert/stream command");
      if (strncmp(modify->fix[f_i]->style,"mesh",4))
        error->fix_error(FLERR,this,"The fix belonging to the id you provided is not of type mesh");
      ins_face = (static_cast<FixMeshSurface*>(modify->fix[f_i]))->triMesh();
      ins_face->useAsInsertionMesh();
      face_style = FACE_MESH;
      iarg += 2;
      hasargs = true;
    }else if (strcmp(arg[iarg],"extrude_length") == 0) {
      if (iarg+2 > narg) error->fix_error(FLERR,this,"");
      extrude_length = atof(arg[iarg+1]);
      if(extrude_length < 0. ) error->fix_error(FLERR,this,"invalid extrude_length");
      iarg += 2;
      hasargs = true;
    }else if (strcmp(arg[iarg],"duration") == 0) {
      if (iarg+2 > narg) error->fix_error(FLERR,this,"");
      duration = atoi(arg[iarg+1]);
      if(duration < 1 ) error->fix_error(FLERR,this,"'duration' can not be < 1");
      iarg += 2;
      hasargs = true;
    }else error->fix_error(FLERR,this,"unknown keyword");
  }

  fix_release = NULL;
  fix_rm = NULL;
  i_am_integrator = false;

  //NP execute end of step
  nevery = 1;
}

/* ---------------------------------------------------------------------- */

FixInsertStream::~FixInsertStream()
{

}

/* ---------------------------------------------------------------------- */

void FixInsertStream::post_create()
{
  FixInsert::post_create();

  // only register property if I am the first fix/insert/stream in the simulation
  //NP 8 values: original position to integrate (3), start step (1),
  //NP           release step (1), integration velocity (3)

  if(modify->n_fixes_style(style) == 1)
  {
        char* fixarg[16];

        fixarg[0]="release_fix_insert_stream";
        fixarg[1]="all";
        fixarg[2]="property/atom";
        fixarg[3]="release_fix_insert_stream";
        fixarg[4]="vector"; //NP 1 scalar per particle to be registered
        fixarg[5]="yes";    //NP restart yes
        fixarg[6]="yes";    //NP communicate ghost no
        fixarg[7]="no";    //NP communicate rev yes
        fixarg[8]="0.";
        fixarg[9]="0.";
        fixarg[10]="0.";
        fixarg[11]="0.";
        fixarg[12]="0.";
        fixarg[13]="0.";
        fixarg[14]="0.";
        fixarg[15]="0.";
        modify->add_fix_property_atom(16,fixarg,style);
  }
}

/* ---------------------------------------------------------------------- */

void FixInsertStream::pre_delete(bool unfixflag)
{
    // delete if I am the last fix of this style to be deleted
    if(modify->n_fixes_style(style) == 1)
    modify->delete_fix("release_fix_insert_stream");
}

/* ---------------------------------------------------------------------- */

void FixInsertStream::init_defaults()
{
    face_style = FACE_NONE;
    extrude_length = 0.;

    duration = 0;
}

/* ----------------------------------------------------------------------
   calculate ninsert, insert_every, ninsert_per, massinsert, flowrates etc
   also perform error checks
------------------------------------------------------------------------- */

void FixInsertStream::calc_insertion_properties()
{
    double dt,dot,extrude_vec[3],t1[3],t2[3];
    double *fnorm;

    // error check on insertion face
    if(face_style == FACE_NONE)
        error->fix_error(FLERR,this,"must define an insertion face");

    // check properties of insertion face
    if(face_style == FACE_MESH)
    {
        // check if face planar
        if(!ins_face->isPlanar())
            error->fix_error(FLERR,this,"command requires a planar face for insertion");

        // get normal vector of face 0
        ins_face->surfaceNorm(0,normalvec);

        //NP printVec3D(screen,"normalvec",normalvec);

        // flip normal vector so dot product with v_insert is > 0
        dot = vectorDot3D(v_insert,normalvec);
        if(dot < 0) vectorScalarMult3D(normalvec,-1.);

        // calc v normal
        dot = vectorDot3D(v_insert,normalvec);
        vectorCopy3D(normalvec,v_normal);
        vectorScalarMult3D(v_normal,dot);

        // error check on v normal
        if(vectorMag3D(v_normal) < 1.e-3)
          error->fix_error(FLERR,this,"insertion velocity projected on face normal is < 1e-3");

        // get reference point on face
        ins_face->node(0,0,p_ref);
    }
    else error->fix_error(FLERR,this,"FixInsertStream::calc_insertion_properties(): Implementation missing");

    // error check on insertion velocity
    if(vectorMag3D(v_insert) < 1e-5)
        error->fix_error(FLERR,this,"insertion velocity too low");

    // further error-checks
    if(insert_every == -1 && extrude_length == 0.)
      error->fix_error(FLERR,this,"must define either 'insert_every' or 'extrude_length'");
    if(insert_every > -1 && extrude_length > 0.)
      error->fix_error(FLERR,this,"must not provide both 'insert_every' and 'extrude_length'");
    if(extrude_length > 0. && duration > 0)
      error->fix_error(FLERR,this,"must not provide both 'extrude_length' and 'duration'");

    dt = update->dt;

    // if extrude_length given, calculate insert_every
    if(insert_every == -1)
    {
        // no duration allowed here (checked before)

        if(extrude_length < 3.*max_r_bound())
            error->fix_error(FLERR,this,"'extrude_length' is too small");
        insert_every = static_cast<int>(extrude_length/(dt*vectorMag3D(v_normal)));
        /*NL*///fprintf(screen,"insert_every %d, extrude_length %f, dt %f, vectorMag3D(v_normal) %f\n",insert_every,extrude_length,dt,vectorMag3D(v_normal));
        if(insert_every == 0)
          error->fix_error(FLERR,this,"insertion velocity too high or extrude_length too low");
    }
    // if insert_every given, calculate extrude_length
    // take into account duration can be != insert_every
    else
    {
        if(insert_every < 1) error->fix_error(FLERR,this,"'insert_every' must be > 0");

        // duration = insert_every by default (if already > 0, defined directly)
        if(duration == 0) duration = insert_every;
        else if (duration > insert_every) error->fix_error(FLERR,this,"'duration' > 'insert_every' not allowed");

        extrude_length = static_cast<double>(duration) * dt * vectorMag3D(v_normal);
        /*NL*/// fprintf(screen,"extrude_length %f, max_r_bound() %f, duration %d\n",extrude_length,max_r_bound(),duration);
        if(extrude_length < 3.*max_r_bound())
          error->fix_error(FLERR,this,"'insert_every' or 'vel' is too small");
    }

    /*NL*///fprintf(screen,"insert_every %d, duration %d, v_normal %f, extrude_length %f dt %e\n",insert_every,duration,vectorMag3D(v_normal),extrude_length,dt);

    // ninsert - if ninsert not defined directly, calculate it
    if(ninsert == 0)
    {
        if(massinsert > 0.) ninsert = static_cast<int>(massinsert / fix_distribution->mass_expect());
        else error->fix_error(FLERR,this,"must define either 'nparticles' or 'mass'");
    }

    // flow rate
    if(nflowrate == 0.)
    {
        if(massflowrate == 0.) error->fix_error(FLERR,this,"must define either 'massrate' or 'particlerate'");
        nflowrate = massflowrate / fix_distribution->mass_expect();
    }
    else massflowrate = nflowrate * fix_distribution->mass_expect();

    // ninsert_per and massinsert
    ninsert_per = nflowrate*(static_cast<double>(insert_every)*dt);
    massinsert = static_cast<double>(ninsert) * fix_distribution->mass_expect();

    // calculate bounding box of extruded face
    if(face_style == FACE_MESH)
    {
        // get bounding box for face
        ins_face->getGlobalBoundingBox().getBoxBounds(ins_vol_xmin,ins_vol_xmax);

        // get bounding box for extruded face - store in t1,t2
        vectorScalarMult3D(normalvec,-extrude_length,extrude_vec);
        vectorAdd3D(ins_vol_xmin,extrude_vec,t1);
        vectorAdd3D(ins_vol_xmax,extrude_vec,t2);

        // take min and max
        vectorComponentMin3D(ins_vol_xmin,t1,ins_vol_xmin);
        vectorComponentMax3D(ins_vol_xmax,t2,ins_vol_xmax);

        /*NL*///printVec3D(screen,"ins_vol_xmin",ins_vol_xmin);
        /*NL*///printVec3D(screen,"ins_vol_xmin",ins_vol_xmax);
    }
    else error->fix_error(FLERR,this,"Missing implementation in calc_insertion_properties()");
}

/* ---------------------------------------------------------------------- */

int FixInsertStream::setmask()
{
    int mask = FixInsert::setmask();
    mask |= END_OF_STEP;
    return mask;
}

/* ---------------------------------------------------------------------- */

void FixInsertStream::init()
{
    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::init() start\n");

    FixInsert::init();

    fix_release = static_cast<FixPropertyAtom*>(modify->find_fix_property("release_fix_insert_stream","property/atom","vector",5,0,style));
    if(!fix_release) error->fix_error(FLERR,this,"Internal error if fix insert/stream");

    i_am_integrator = modify->i_am_first_of_style(this);

    //NP currently only one fix rigid allowed
    if(fix_rm) fix_rm->set_v_integrate(v_normal);
    if(!i_am_integrator && fix_rm)
        error->fix_error(FLERR,this,"Currently only one fix insert/stream is allowed with multisphere particles");

    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::init() end\n");
}

/* ---------------------------------------------------------------------- */

double FixInsertStream::insertion_fraction()
{
    return ins_face->areaMeshSubdomain()/ins_face->areaMeshGlobal() ;
}

/* ---------------------------------------------------------------------- */

void FixInsertStream::pre_insert()
{
    if(!domain->is_in_domain(ins_vol_xmin) || !domain->is_in_domain(ins_vol_xmax))
      error->warning(FLERR,"Fix insert/stream: Extruded insertion face extends outside domain, may not insert all particles correctly");

    //NP should do error check as in FixInsertPack::calc_ninsert_this() here
}

/* ---------------------------------------------------------------------- */

inline int FixInsertStream::is_nearby(int i)
{
    double pos_rel[3], pos_projected[3], t[3];
    double **x = atom->x;

    /*NL*///if(atom->tag[i] == 230) fprintf(screen,"pref = %f %f %f\n",p_ref[0],p_ref[1],p_ref[2]);

    vectorSubtract3D(x[i],p_ref,pos_rel);
    double dist_normal = vectorDot3D(pos_rel,normalvec);

    /*NL*///if(atom->tag[i] == 230) fprintf(screen,"pos_rel %f %f %f\n",pos_rel[0],pos_rel[1],pos_rel[2]);
    /*NL*///if(atom->tag[i] == 230) fprintf(screen,"dist_normal %f  extrude_length %f maxrad %f\n",dist_normal,extrude_length,maxrad);

    // on wrong side of extrusion
    if(dist_normal > maxrad) return 0;

    /*NL*///fprintf(screen,"1\n");

    // on right side of extrusion, but too far away
    // 3*maxrad b/c extrude_length+rad is max extrusion for overlapcheck yes
    if(dist_normal < -(extrude_length + 3.*maxrad)) return 0;

    /*NL*///fprintf(screen,"2\n");

    // on right side of extrusion, within extrude_length
    // check if projection is on face or not

    vectorScalarMult3D(normalvec,dist_normal,t);
    vectorAdd3D(x[i],t,pos_projected);

    //TODO also should check if projected point is NEAR surface

    return ins_face->isOnSurface(pos_projected);
}

/* ----------------------------------------------------------------------
   generate random positions on insertion face
   extrude by random length in negative face normal direction
     currently only implemented for all_in_flag = 0
     since would be tedious to check/implement otherwise
------------------------------------------------------------------------- */

inline void FixInsertStream::generate_random(double *pos, double rad)
{
    double r, ext[3];

    // generate random position on the mesh
    //NP position has to be within the subbox
    if(all_in_flag)
        ins_face->generateRandomSubboxWithin(pos,rad);
    else
        ins_face->generateRandomSubbox(pos);

    // extrude the position
    //NP min pos: max_rbound,
    //NP max pos: extrude_length - max_rbound or extrude_length
    //NP   extrude_length - max_rbound for strict non-overlapping
    //NP   if overlap is checked for, can go a bit beyond so
    //NP   stream is more continuous

    if(check_ol_flag)
        r = -1.*(random->uniform()*(extrude_length         ) + rad);
    else
        r = -1.*(random->uniform()*(extrude_length - 2.*rad) + rad);

    vectorScalarMult3D(normalvec,r,ext);
    vectorAdd3D(pos,ext,pos);
}

/* ----------------------------------------------------------------------
   generate random positions within extruded face
   perform overlap check via xnear if requested
   returns # bodies and # spheres that could actually be inserted
------------------------------------------------------------------------- */

void FixInsertStream::x_v_omega(int ninsert_this_local,int &ninserted_this_local, int &ninserted_spheres_this_local, double &mass_inserted_this_local)
{
    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::x_v_omega() start\n");

    ninserted_this_local = ninserted_spheres_this_local = 0;
    mass_inserted_this_local = 0.;

    int nins;
    double pos[3];
    ParticleToInsert *pti;

    double omega_tmp[] = {0.,0.,0.};

    // no overlap check
    // insert with v_normal, no omega
    if(!check_ol_flag)
    {
        for(int itotal = 0; itotal < ninsert_this_local; itotal++)
        {
            pti = fix_distribution->pti_list[ninserted_this_local];
            double rad_to_insert = pti->r_bound_ins;
            generate_random(pos,rad_to_insert);

            // could randomize vel, omega, quat here

            nins = pti->set_x_v_omega(pos,v_normal,omega_tmp,quat_insert);

            ninserted_spheres_this_local += nins;
            mass_inserted_this_local += pti->mass_ins;
            ninserted_this_local++;
        }
    }
    // overlap check
    // account for maxattempt
    // pti checks against xnear and adds self contributions
    else
    {
        int ntry = 0;
        int maxtry = ninsert_this_local * maxattempt;

        while(ntry < maxtry && ninserted_this_local < ninsert_this_local)
        {
            pti = fix_distribution->pti_list[ninserted_this_local];
            double rad_to_insert = pti->r_bound_ins;

            nins = 0;
            while(nins == 0 && ntry < maxtry)
            {
                do
                {
                    generate_random(pos,rad_to_insert);
                    ntry++;
                    /*NL*///fprintf(screen,"domain->dist_subbox_borders(pos) %f\n",domain->dist_subbox_borders(pos));
                }
                while(ntry < maxtry && domain->dist_subbox_borders(pos) < rad_to_insert);

                // could randomize vel, omega, quat here

                nins = pti->check_near_set_x_v_omega(pos,v_normal,omega_tmp,quat_insert,xnear,nspheres_near);
            }

            if(nins > 0)
            {
                ninserted_spheres_this_local += nins;
                mass_inserted_this_local += pti->mass_ins;
                ninserted_this_local++;
            }
        }
    }

    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::x_v_omega() end\n");
}

/* ---------------------------------------------------------------------- */

void FixInsertStream::finalize_insertion(int ninserted_spheres_this_local)
{
    // nins particles have been inserted on this proc, set initial position, insertion step and release step according to pos

    int n_steps = -1;
    int step = update->ntimestep;
    int ilo = atom->nlocal - ninserted_spheres_this_local;
    int ihi = atom->nlocal;

    double pos_rel[3], dist_normal;
    double **x = atom->x;
    double dt = update->dt;

    double **release_data = fix_release->array_atom;

    Multisphere *multisphere = NULL;
    if(fix_rm) multisphere = &fix_rm->data();

    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::finalize_insertion() start, ilo %d, ihi %d, nlocal %d\n",ilo,ihi,atom->nlocal);

    for(int i = ilo; i < ihi; i++)
    {
        /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::finalize_insertion() i %d, 0\n",i);
        if(multisphere)
            n_steps = multisphere->calc_n_steps(i,p_ref,normalvec,v_normal);
        if(!multisphere || n_steps == -1)
        {
            vectorSubtract3D(p_ref,x[i],pos_rel);
            dist_normal = vectorDot3D(pos_rel,normalvec);
            n_steps = static_cast<int>(dist_normal/(vectorMag3D(v_normal)*dt));
        }

        // first 3 values is original position to integrate
        vectorCopy3D(x[i],release_data[i]);

        /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::finalize_insertion() i %d, 1\n",i);

        // 4th value is insertion step
        release_data[i][3] = static_cast<double>(step);

        // 5th value is step to release
        release_data[i][4] = static_cast<double>(step + n_steps);

        // 6-8th value is integration velocity
        vectorCopy3D(v_normal,&release_data[i][5]);

        /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::finalize_insertion() i %d, 2\n",i);
    }

    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::finalize_insertion() end\n");
}

/* ---------------------------------------------------------------------- */

void FixInsertStream::end_of_step()
{
    int r_step, i_step;

    int step = update->ntimestep;
    int nlocal = atom->nlocal;
    double **release_data = fix_release->array_atom;
    double time_elapsed, dist_elapsed[3], v_integrate[3];
    double dt = update->dt;

    double **x = atom->x;
    double **v = atom->v;
    double **f = atom->f;
    double **omega = atom->omega;
    double **torque = atom->torque;
    int *mask = atom->mask;

    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::end_of_step() start\n");

    // only one fix handles the integration
    if(!i_am_integrator) return;

    for(int i = 0; i < nlocal; i++)
    {
        if (mask[i] & groupbit)
        {
            if(release_data[i][3] == 0.) continue;

            i_step = static_cast<int>(release_data[i][3]);
            r_step = static_cast<int>(release_data[i][4]);
            vectorCopy3D(&release_data[i][5],v_integrate);

            if(step > r_step) continue;
            else if (r_step == step)
            {
                //NP dont do this for multisphere
                if(fix_rm && fix_rm->belongs_to(i) >= 0) continue;

                // integrate with constant vel and set v,omega

                time_elapsed = (step - i_step) * dt;

                // particle moves with v_integrate
                vectorScalarMult3D(v_integrate,time_elapsed,dist_elapsed);
                double *x_ins = release_data[i];

                // set x,v,omega
                vectorAdd3D(x_ins,dist_elapsed,x[i]);
                vectorCopy3D(v_integrate,v[i]);
                vectorZeroize3D(omega[i]);

                // zero out force, torque
                vectorZeroize3D(f[i]);
                vectorZeroize3D(torque[i]);

                // set inital conditions
                vectorCopy3D(v_integrate,v[i]);
                vectorCopy3D(omega_insert,omega[i]);

                /*NL*///if(fix_rm) error->one(FLERR,"must set params for frm");
            }
            // step < r_step, only true for inserted particles
            //   b/c r_step is 0 for all other particles
            // integrate with constant vel
            else
            {
                time_elapsed = (step - i_step) * dt;

                // particle moves with v_integrate
                vectorScalarMult3D(v_integrate,time_elapsed,dist_elapsed);
                double *x_ins = release_data[i];

                // set x,v,omega
                vectorAdd3D(x_ins,dist_elapsed,x[i]);
                vectorCopy3D(v_integrate,v[i]);
                vectorZeroize3D(omega[i]);

                // zero out force, torque
                vectorZeroize3D(f[i]);
                vectorZeroize3D(torque[i]);
            }
        }
    }

    /*NL*/ if(LMP_DEBUGMODE_FIXINSERT_STREAM) fprintf(LMP_DEBUG_OUT_FIXINSERT_STREAM,"FixInsertStream::end_of_step() end\n");
}

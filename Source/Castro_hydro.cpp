#include "Castro.H"
#include "Castro_F.H"

using namespace amrex;

void
Castro::construct_mol_hydro_source(Real time, Real dt, int istage, int nstages)
{

  BL_PROFILE_VAR("Castro::construct_mol_hydro_source()", CA_HYDRO);

  // this constructs the hydrodynamic source (essentially the flux
  // divergence) using method of lines integration.  The output, as a
  // update to the state, is stored in the k_mol array of multifabs.

  int finest_level = parent->finestLevel();

  const Real *dx = geom.CellSizeF();

  MultiFab& S_new = get_new_data(State_Type);

  MultiFab& k_stage = *k_mol[istage];

  MultiFab q;
  q.define(grids, dmap, NQ, NUM_GROW);

  MultiFab qaux;
  qaux.define(grids, dmap, NQAUX, NUM_GROW);

  MultiFab flatn;
  flatn.define(grids, dmap, 1, 1);

  MultiFab div;
  div.define(grids, dmap, 1, 1);

  MultiFab qm;
  qm.define(grids, dmap, 3*NQ, 2);

  MultiFab qp;
  qp.define(grids, dmap, 3*NQ, 2);

  MultiFab flux[BL_SPACEDIM];
  MultiFab qe[BL_SPACEDIM];

  for (int i = 0; i < BL_SPACEDIM; ++i) {
      flux[i].define(getEdgeBoxArray(i), dmap, NUM_STATE, 0);
      qe[i].define(getEdgeBoxArray(i), dmap, NGDNV, 0);
  }

  const int*  domain_lo = geom.Domain().loVect();
  const int*  domain_hi = geom.Domain().hiVect();

#ifdef _OPENMP
#pragma omp parallel
#endif
  for (MFIter mfi(S_new, hydro_tile_size); mfi.isValid(); ++mfi) {

      const Box& qbx = mfi.growntilebox(NUM_GROW);

      // Convert the conservative state to the primitive variable state.
      // This fills both q and qaux.

      FORT_LAUNCH(qbx, ca_ctoprim,
                  BL_TO_FORTRAN_BOX(qbx),
                  BL_TO_FORTRAN_ANYD(Sborder[mfi]),
                  BL_TO_FORTRAN_ANYD(q[mfi]),
                  BL_TO_FORTRAN_ANYD(qaux[mfi]));

  } // MFIter loop

#ifdef _OPENMP
#pragma omp parallel
#endif
  for (MFIter mfi(S_new, hydro_tile_size); mfi.isValid(); ++mfi) {

      const Box& obx = mfi.growntilebox(1);

      // Compute divergence of velocity field.

      FORT_LAUNCH(obx, ca_divu,
                  BL_TO_FORTRAN_BOX(obx),
                  dx,
                  BL_TO_FORTRAN_ANYD(q[mfi]),
                  BL_TO_FORTRAN_ANYD(div[mfi]));

      // Compute flattening coefficient for slope calculations.
      FORT_LAUNCH(obx, ca_uflaten,
                  BL_TO_FORTRAN_BOX(obx),
                  BL_TO_FORTRAN_ANYD(q[mfi]),
                  BL_TO_FORTRAN_ANYD(flatn[mfi]));

      // Do PPM reconstruction to the zone edges.
      FORT_LAUNCH(obx, ca_ppm_reconstruct,
                  BL_TO_FORTRAN_BOX(obx),
                  BL_TO_FORTRAN_ANYD(q[mfi]),
                  BL_TO_FORTRAN_ANYD(flatn[mfi]),
                  BL_TO_FORTRAN_ANYD(qm[mfi]),
                  BL_TO_FORTRAN_ANYD(qp[mfi]));

  } // MFIter loop



#ifdef _OPENMP
#pragma omp parallel
#endif
  for (MFIter mfi(S_new, hydro_tile_size); mfi.isValid(); ++mfi) {

      for (int idir = 0; idir < BL_SPACEDIM; ++idir) {

          const Box& ebx = mfi.nodaltilebox(idir);

          int idir_f = idir + 1;

          FORT_LAUNCH(ebx, ca_construct_flux,
                      BL_TO_FORTRAN_BOX(ebx),
                      domain_lo, domain_hi,
                      dx, dt,
                      idir_f,
                      BL_TO_FORTRAN_ANYD(Sborder[mfi]),
                      BL_TO_FORTRAN_ANYD(div[mfi]),
                      BL_TO_FORTRAN_ANYD(qaux[mfi]),
                      BL_TO_FORTRAN_ANYD(qm[mfi]),
                      BL_TO_FORTRAN_ANYD(qp[mfi]),
                      BL_TO_FORTRAN_ANYD(qe[idir][mfi]),
                      BL_TO_FORTRAN_ANYD(flux[idir][mfi]),
                      BL_TO_FORTRAN_ANYD(area[idir][mfi]));

          // Store the fluxes from this advance -- we weight them by the
          // integrator weight for this stage
          (*fluxes[idir])[mfi].saxpy(b_mol[istage], flux[idir][mfi], ebx, ebx, 0, 0, NUM_STATE);

      }

  } // MFIter loop


#ifdef _OPENMP
#pragma omp parallel
#endif
  for (MFIter mfi(S_new, hydro_tile_size); mfi.isValid(); ++mfi) {

      const Box& bx = mfi.tilebox();

      FORT_LAUNCH(bx, ca_construct_hydro_update,
                  BL_TO_FORTRAN_BOX(bx),
                  dx, dt,
                  b_mol[istage],
                  BL_TO_FORTRAN_ANYD(qe[0][mfi]),
                  BL_TO_FORTRAN_ANYD(qe[1][mfi]),
                  BL_TO_FORTRAN_ANYD(qe[2][mfi]),
                  BL_TO_FORTRAN_ANYD(flux[0][mfi]),
                  BL_TO_FORTRAN_ANYD(flux[1][mfi]),
                  BL_TO_FORTRAN_ANYD(flux[2][mfi]),
                  BL_TO_FORTRAN_ANYD(area[0][mfi]),
                  BL_TO_FORTRAN_ANYD(area[1][mfi]),
                  BL_TO_FORTRAN_ANYD(area[2][mfi]),
                  BL_TO_FORTRAN_ANYD(volume[mfi]),
                  BL_TO_FORTRAN_ANYD(hydro_source[mfi]));

  } // MFIter loop

  BL_PROFILE_VAR_STOP(CA_HYDRO);

  // Flush Fortran output

  if (verbose)
    flush_output();

}

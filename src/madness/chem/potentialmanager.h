/*
  This file is part of MADNESS.

  Copyright (C) 2007,2010 Oak Ridge National Laboratory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680

  $Id$
*/
#ifndef MADNESS_CHEM_POTENTIALMANAGER_H__INCLUDED
#define MADNESS_CHEM_POTENTIALMANAGER_H__INCLUDED

/// \file moldft/potentialmanager.h
/// \brief Declaration of molecule related classes and functions

#include<madness/chem/corepotential.h>
#include<madness/chem/atomutil.h>
#include<madness/chem/molecule.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctype.h>
#include <cmath>
#include <madness/tensor/tensor.h>
#include <madness/misc/misc.h>
#include <madness/mra/mra.h>

namespace madness {
class MolecularPotentialFunctor : public FunctionFunctorInterface<double,3> {
private:
    const Molecule& molecule;
public:
    MolecularPotentialFunctor(const Molecule& molecule)
        : molecule(molecule) {}

    double operator()(const coord_3d& x) const {
        return molecule.nuclear_attraction_potential(x[0], x[1], x[2]);
    }

    std::vector<coord_3d> special_points() const {return molecule.get_all_coords_vec();}
};

class MolecularCorePotentialFunctor : public FunctionFunctorInterface<double,3> {
private:
    const Molecule& molecule;
public:
    MolecularCorePotentialFunctor(const Molecule& molecule)
        : molecule(molecule) {}

    double operator()(const coord_3d& x) const {
        return molecule.molecular_core_potential(x[0], x[1], x[2]);
    }

    std::vector<coord_3d> special_points() const {return molecule.get_all_coords_vec();}
};

class CoreOrbitalFunctor : public FunctionFunctorInterface<double,3> {
    const Molecule molecule;
    const int atom;
    const unsigned int core;
    const int m;
public:
    CoreOrbitalFunctor(Molecule& molecule, int atom, unsigned int core, int m)
        : molecule(molecule), atom(atom), core(core), m(m) {};
    double operator()(const coord_3d& r) const {
        return molecule.core_eval(atom, core, m, r[0], r[1], r[2]);
    };
};

class CoreOrbitalDerivativeFunctor : public FunctionFunctorInterface<double,3> {
    const Molecule molecule;
    const int atom, axis;
    const unsigned int core;
    const int m;
public:
    CoreOrbitalDerivativeFunctor(Molecule& molecule, int atom, int axis, unsigned int core, int m)
        : molecule(molecule), atom(atom), axis(axis), core(core), m(m) {};
    double operator()(const coord_3d& r) const {
        return molecule.core_derivative(atom, axis, core, m, r[0], r[1], r[2]);
    };
};

// utility type conversion function
inline Tensor<double> array_to_tensor(const std::array<std::array<double, 2>, 3> arr) {
  Tensor<double> tensor(3, 2);
  for (size_t i = 0; i < 3; i++) {
    for (size_t j = 0; j < 2; j++) {
      tensor(i, j) = arr[i][j];
    }
  }
  return tensor;
}

class NuclearDensityFunctor : public FunctionFunctorInterface<double,3> {
private:
  const Molecule& molecule;
  const Tensor<double> cell;
  std::vector<coord_3d> specialpt;
  std::array<std::vector<int>, 3> allowed_increments;
public:
  // Perform a type conversion. Needed by MPQC.
  NuclearDensityFunctor(const Molecule& molecule, const BoundaryConditions<3> bc, const std::array<std::array<double, 2>, 3> cell) :
    NuclearDensityFunctor(molecule, bc, array_to_tensor(cell)){}
  NuclearDensityFunctor(const Molecule& molecule, const BoundaryConditions<3> bc, const Tensor<double> cell)
      : molecule(molecule), cell(cell), specialpt(molecule.get_all_coords_vec())
  {
    for (int i = 0; i < 3; i++ ) {
      allowed_increments[i] = {0};
      if (bc(i, 0) == BC_PERIODIC) {
        allowed_increments[i].push_back(-1);
      }
      if (bc(i, 1) == BC_PERIODIC) {
        allowed_increments[i].push_back(1);
      }
    }
  }

  double operator()(const coord_3d& x) const {
    double tol = 6.0 * molecule.smallest_length_scale();
    double sum = 0.0;
    double xdisp = cell(0, 1) - cell(0, 0);
    double ydisp = cell(1, 1) - cell(1, 0);
    double zdisp = cell(2, 1) - cell(2, 0);
    // TODO: Check if the if statements do anything. The nuclear_charge_density filter
    //       of large rvalues may make it redundant.
    for (const auto xinc: allowed_increments[0]) {
      double xtgt = x[0]+xinc*xdisp;
      if (xtgt < cell(0, 1) + tol && xtgt > cell(0, 0) - tol) {
        for (const auto yinc: allowed_increments[1]) {
          double ytgt = x[1]+yinc*ydisp;
          if (ytgt < cell(1, 1) + tol && ytgt > cell(1, 0) - tol) {
            for (const auto zinc: allowed_increments[2]) {
              double ztgt = x[2]+zinc*zdisp;
              if (ztgt < cell(2, 1) + tol && xtgt > cell(2, 0) - tol)
                sum += molecule.nuclear_charge_density(xtgt, ytgt, ztgt);
            }
          }
        }
      }
    }
    return sum;
  }

  std::vector<coord_3d> special_points() const {return specialpt;}

  Level special_level() {
    return 50;
  }

};


class PotentialManager {
private:
Molecule mol;
real_function_3d vnuc;
std::string core_type_;

public:
    PotentialManager(const Molecule& molecule, const std::string& core_type)
     : mol(molecule), core_type_(core_type) {}

    const Molecule& molecule() const {
      return this->mol;
    }

    const std::string& core_type() const {
      return this->core_type_;
    }

    const real_function_3d& vnuclear() {
        return vnuc;
    }

    vector_real_function_3d core_projection(World & world, const vector_real_function_3d& psi, const bool include_Bc = true)
    {
        int npsi = psi.size();
        if (npsi == 0) return psi;
        int natom = mol.natom();
        vector_real_function_3d proj = zero_functions_compressed<double,3>(world, npsi);
        real_tensor overlap_sum(static_cast<long>(npsi));

        for (int i=0; i<natom; ++i) {
            Atom at = mol.get_atom(i);
            unsigned int atn = at.atomic_number;
            unsigned int nshell = mol.n_core_orb(atn);
            if (nshell == 0) continue;
            for (unsigned int c=0; c<nshell; ++c) {
                unsigned int l = mol.get_core_l(atn, c);
                int max_m = (l+1)*(l+2)/2;
                nshell -= max_m - 1;
                for (int m=0; m<max_m; ++m) {
                    real_function_3d core = real_factory_3d(world).functor(real_functor_3d(new CoreOrbitalFunctor(mol, i, c, m)));
                    real_tensor overlap = inner(world, core, psi);
                    overlap_sum += overlap;
                    for (int j=0; j<npsi; ++j) {
                        if (include_Bc) overlap[j] *= mol.get_core_bc(atn, c);
                        proj[j] += core.scale(overlap[j]);
                    }
                }
            }
            world.gop.fence();
        }
        if (world.rank() == 0) print("sum_k <core_k|psi_i>:", overlap_sum);
        return proj;
    }

    double core_projector_derivative(World & world, const vector_real_function_3d& mo, const real_tensor& occ, int atom, int axis)
    {
        vector_real_function_3d cores, dcores;
        std::vector<double> bc;
        unsigned int atn = mol.get_atom(atom).atomic_number;
        unsigned int ncore = mol.n_core_orb(atn);

        // projecting core & d/dx core
        for (unsigned int c=0; c<ncore; ++c) {
            unsigned int l = mol.get_core_l(atn, c);
            int max_m = (l+1)*(l+2)/2;
            for (int m=0; m<max_m; ++m) {
                real_functor_3d func = real_functor_3d(new CoreOrbitalFunctor(mol, atom, c, m));
                cores.push_back(real_function_3d(real_factory_3d(world).functor(func).truncate_on_project()));
                func = real_functor_3d(new CoreOrbitalDerivativeFunctor(mol, atom, axis, c, m));
                dcores.push_back(real_function_3d(real_factory_3d(world).functor(func).truncate_on_project()));
                bc.push_back(mol.get_core_bc(atn, c));
            }
        }

        // calc \sum_i occ_i <psi_i|(\sum_c Bc d/dx |core><core|)|psi_i>
        double r = 0.0;
        for (unsigned int c=0; c<cores.size(); ++c) {
            double rcore= 0.0;
            real_tensor rcores = inner(world, cores[c], mo);
            real_tensor rdcores = inner(world, dcores[c], mo);
            for (unsigned int i=0; i<mo.size(); ++i) {
                rcore += rdcores[i] * rcores[i] * occ[i];
            }
            r += 2.0 * bc[c] * rcore;
        }

        return r;
    }

    void apply_nonlocal_potential(World& world, const vector_real_function_3d& amo, vector_real_function_3d Vpsi) {
        if (core_type_.substr(0,3) == "mcp") {
         //   START_TIMER(world);
            gaxpy(world, 1.0, Vpsi, 1.0, core_projection(world, amo));
         //   END_TIMER(world, "MCP Core Projector");
        }
    }

    void make_nuclear_potential(World& world) {
        double safety = 0.1;
        double vtol = FunctionDefaults<3>::get_thresh() * safety;
        vnuc = real_factory_3d(world).functor(real_functor_3d(new MolecularPotentialFunctor(mol))).thresh(vtol).truncate_on_project();
        vnuc.set_thresh(FunctionDefaults<3>::get_thresh());
        vnuc.reconstruct();
        //     "" is  legacy core_type value for all-electron (also be used by CorePotentialManager)
        // "none" is current core_type value for all-electron
        if (core_type_ != "" && core_type_ != "none") {
            real_function_3d c_pot = real_factory_3d(world).functor(real_functor_3d(new MolecularCorePotentialFunctor(mol))).thresh(vtol).initial_level(4);
            c_pot.set_thresh(FunctionDefaults<3>::get_thresh());
            c_pot.reconstruct();
            vnuc += c_pot;
            vnuc.truncate();
        }
    }
};
}

#endif

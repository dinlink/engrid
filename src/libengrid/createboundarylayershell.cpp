// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// +                                                                      +
// + This file is part of enGrid.                                         +
// +                                                                      +
// + Copyright 2008-2014 enGits GmbH                                      +
// +                                                                      +
// + enGrid is free software: you can redistribute it and/or modify       +
// + it under the terms of the GNU General Public License as published by +
// + the Free Software Foundation, either version 3 of the License, or    +
// + (at your option) any later version.                                  +
// +                                                                      +
// + enGrid is distributed in the hope that it will be useful,            +
// + but WITHOUT ANY WARRANTY; without even the implied warranty of       +
// + MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        +
// + GNU General Public License for more details.                         +
// +                                                                      +
// + You should have received a copy of the GNU General Public License    +
// + along with enGrid. If not, see <http://www.gnu.org/licenses/>.       +
// +                                                                      +
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#include "createboundarylayershell.h"

#include "createvolumemesh.h"
#include "swaptriangles.h"
#include "deletetetras.h"
#include "deletecells.h"
#include "meshpartition.h"
#include "deletevolumegrid.h"

CreateBoundaryLayerShell::CreateBoundaryLayerShell()
{
  m_RestGrid      = vtkSmartPointer<vtkUnstructuredGrid>::New();
  m_OriginalGrid  = vtkSmartPointer<vtkUnstructuredGrid>::New();
  m_PrismaticGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
}

void CreateBoundaryLayerShell::prepare()
{
  m_Part.trackGrid(m_Grid);

  DeleteVolumeGrid delete_volume;
  delete_volume.setGrid(m_Grid);
  delete_volume.setAllCells();
  delete_volume();

  readSettings();
  setAllCells();
  getSurfaceCells(m_BoundaryLayerCodes, layer_cells, m_Grid);

  // fill m_LayerAdjacentBoundaryCodes
  EG_VTKDCC(vtkIntArray, cell_code, m_Grid, "cell_code");
  foreach (vtkIdType id_cell, layer_cells) {
    for (int i = 0; i < m_Part.c2cGSize(id_cell); ++i) {
      vtkIdType id_neigh = m_Part.c2cGG(id_cell, i);
      int bc = cell_code->GetValue(id_neigh);
      if (!m_BoundaryLayerCodes.contains(bc)) {
        m_LayerAdjacentBoundaryCodes.insert(bc);
      }
    }
  }

  // compute normals and origins of adjacent planes
  m_LayerAdjacentNormals.clear();
  m_LayerAdjacentOrigins.clear();
  foreach (int bc, m_LayerAdjacentBoundaryCodes) {
    double L = EG_LARGE_REAL;
    vec3_t n0(0, 0, 0);
    vec3_t x0(0, 0, 0);
    double total_area = 0;
    for (vtkIdType id_cell = 0; id_cell < m_Grid->GetNumberOfCells(); ++id_cell) {
      if (isSurface(id_cell, m_Grid) && cell_code->GetValue(id_cell) == bc) {
        vec3_t n = cellNormal(m_Grid, id_cell);
        double A = n.abs();
        total_area += A;
        n0 += n;
        x0 += A*cellCentre(m_Grid, id_cell);
        L = min(L, sqrt(4*A/sqrt(3.0)));
      }
    }
    n0.normalise();
    x0 *= 1.0/total_area;
    for (vtkIdType id_cell = 0; id_cell < m_Grid->GetNumberOfCells(); ++id_cell) {
      if (isSurface(id_cell, m_Grid) && cell_code->GetValue(id_cell) == bc) {
        vec3_t x = cellCentre(m_Grid, id_cell);
        double l = fabs((x - x0)*n0);
        if (l > 0.1*L) {
          BoundaryCondition boundary_condition = GuiMainWindow::pointer()->getBC(bc);
          QString err_msg = "The boundary \"" + boundary_condition.getName() + "\" is not planar.";
          EG_ERR_RETURN(err_msg);
        }
      }
    }
    m_LayerAdjacentNormals[bc] = n0;
    m_LayerAdjacentOrigins[bc] = x0;
  }

  computeBoundaryLayerVectors();
  makeCopy(m_Grid, m_OriginalGrid);
}

void CreateBoundaryLayerShell::correctAdjacentBC(int bc)
{
  EG_VTKDCC(vtkIntArray, cell_code, m_Grid, "cell_code");
  vec3_t n0 = m_LayerAdjacentNormals[bc];
  vec3_t x0 = m_LayerAdjacentOrigins[bc];
  double scal_min = -1;
  int count = 0;
  while (scal_min < 0.5 && count < 20) {
    scal_min = 1;
    for (vtkIdType id_node = 0; id_node < m_Grid->GetNumberOfPoints(); ++id_node) {
      if (m_Part.n2bcGSize(id_node) == 1) {
        if (m_Part.n2bcG(id_node, 0) == bc) {
          vec3_t x(0,0,0);
          for (int i = 0; i < m_Part.n2nGSize(id_node); ++i) {
            vec3_t xn;
            m_Grid->GetPoint(m_Part.n2nGG(id_node, i), xn.data());
            x += xn;
          }
          x *= 1.0/m_Part.n2nGSize(id_node);
          x -= ((x - x0)*n0)*n0;
          m_Grid->GetPoints()->SetPoint(id_node, x.data());
          for (int i = 0; i < m_Part.n2cGSize(id_node); ++i) {
            vtkIdType id_cell = m_Part.n2cGG(id_node, i);
            if (isSurface(id_cell, m_Grid)) {
              if (cell_code->GetValue(id_cell) == bc) {
                vec3_t n = cellNormal(m_Grid, id_cell);
                n.normalise();
                scal_min = min(scal_min, n*n0);
              }
            }
          }
        }
      }
    }
    ++count;
  }
}

void CreateBoundaryLayerShell::createPrismaticGrid()
{
  QVector<vtkIdType> original_triangles, shell_triangles;
  getSurfaceCells(m_BoundaryLayerCodes, original_triangles, m_OriginalGrid);
  getSurfaceCells(m_BoundaryLayerCodes, shell_triangles, m_Grid);
  {
    MeshPartition part(m_Grid);
    part.setCells(shell_triangles);
    allocateGrid(m_PrismaticGrid, 2*part.getNumberOfCells(), 2*part.getNumberOfNodes());
  }

  QVector<vtkIdType> node_map(m_Grid->GetNumberOfPoints(), -1);
  MeshPartition shell_part(m_Grid);
  shell_part.setCells(shell_triangles);
  for (int i = 0; i < shell_part.getNumberOfNodes(); ++i) {
    node_map[shell_part.globalNode(i)] = i;
  }

  QVector<QList<int> > n2bc(m_PrismaticGrid->GetNumberOfPoints());

  for (vtkIdType id_node = 0; id_node < m_Grid->GetNumberOfPoints(); ++id_node) {
    if (node_map[id_node] != -1) {
      vec3_t x;
      m_Grid->GetPoint(id_node, x.data());
      m_PrismaticGrid->GetPoints()->SetPoint(node_map[id_node], x.data());
      x += m_BoundaryLayerVectors[id_node];
      m_PrismaticGrid->GetPoints()->SetPoint(shell_part.getNumberOfNodes() + node_map[id_node], x.data());
      m_Grid->GetPoints()->SetPoint(id_node, x.data());
      for (int i = 0; i < m_Part.n2bcGSize(id_node); ++i) {
        n2bc[node_map[id_node]].append(m_Part.n2bcG(id_node, i));
        n2bc[node_map[id_node] + shell_part.getNumberOfNodes()].append(m_Part.n2bcG(id_node, i));
      }
    }
  }

  EG_VTKDCC(vtkIntArray, cell_code_src, m_Grid, "cell_code");
  EG_VTKDCC(vtkIntArray, cell_code_dst, m_PrismaticGrid, "cell_code");

  foreach (vtkIdType id_cell, shell_triangles) {
    vtkIdType num_pts, *pts;
    m_Grid->GetCellPoints(id_cell, num_pts, pts);
    vtkIdType tri_pts[3], pri_pts[6];
    for (int i = 0; i < 3; ++i) {
      if (node_map[pts[i]] < 0) {
        EG_BUG;
      }
      if (node_map[pts[i]] >= shell_part.getNumberOfNodes()) {
        EG_BUG;
      }
      tri_pts[i]     = node_map[pts[i]];
      pri_pts[i]     = node_map[pts[i]];
      pri_pts[i + 3] = node_map[pts[i]] + shell_part.getNumberOfNodes();
    }
    vtkIdType id_tri = m_PrismaticGrid->InsertNextCell(VTK_TRIANGLE, 3, tri_pts);
    vtkIdType id_pri = m_PrismaticGrid->InsertNextCell(VTK_WEDGE, 6, pri_pts);
    copyCellData(m_Grid, id_cell, m_PrismaticGrid, id_tri);
  }

  /*
  MeshPartition first_prism_part(m_PrimaticGrid, true);
  m_Part.addPartition(first_prism_part);
  */
}

void CreateBoundaryLayerShell::finalise()
{
}

void CreateBoundaryLayerShell::operate()
{
  prepare();
  //writeBoundaryLayerVectors("blayer");
  createPrismaticGrid();

  foreach (int bc, m_LayerAdjacentBoundaryCodes) {
    correctAdjacentBC(bc);
  }


  // ...

  //finalise();
}

//
// C++ Implementation: showinfo
//
// Description: 
//
//
// Author: Mike Taverne <mtaverne@engits.com>, (C) 2009
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "showinfo.h"

ShowInfo::ShowInfo(bool b, vtkIdType P, vtkIdType C) : Operation()
{
  CellInfo=b;
  PickedPoint=P;
  PickedCell=C;
}


// ShowInfo::~ShowInfo()
// {
// }


void ShowInfo::operate()
{
  int N_cells=grid->GetNumberOfCells();
  int N_points=grid->GetNumberOfPoints();
  if(CellInfo)
  {
    if(PickedCell>=0 && PickedCell<N_cells)
    {
      cout<<"=== INFO ON CELL "<<PickedCell<<" ==="<<endl;
      cout<<"c2c[PickedCell]="<<c2c[PickedCell]<<endl;
      vtkIdType *pts, N_pts;
      grid->GetCellPoints(PickedCell, N_pts, pts);
      cout<<"pts=";
      for(int i=0;i<N_pts;i++) cout<<pts[i]<<" ";
      cout<<endl;
      cout<<"====================================="<<endl;
    }
    else
    {
      cout<<"Invalid cell"<<endl;
    }
  }
  else
  {
    if(PickedPoint>=0 && PickedPoint<N_points)
    {
      cout<<"=== INFO ON POINT "<<PickedPoint<<" ==="<<endl;
      cout<<"n2n[PickedPoint]="<<n2n[PickedPoint]<<endl;
      cout<<"n2c[PickedPoint]="<<n2c[PickedPoint]<<endl;
      cout<<"====================================="<<endl;
    }
    else
    {
      cout<<"Invalid point"<<endl;
    }
  }
}
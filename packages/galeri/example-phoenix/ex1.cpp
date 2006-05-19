#include "Galeri_ConfigDefs.h"
#ifdef HAVE_MPI
#include "mpi.h"
#include "Epetra_MpiComm.h"
#else
#include "Epetra_SerialComm.h"
#endif
#include "Epetra_FECrsMatrix.h"
#include "AztecOO.h"

#include "../src-phoenix/phx_core_Object.h"
#include "../src-phoenix/phx_core_Utils.h"
#include "../src-phoenix/phx_grid_Element.h"
#include "../src-phoenix/phx_grid_Segment.h"
#include "../src-phoenix/phx_grid_Triangle.h"
#include "../src-phoenix/phx_grid_Quad.h"
#include "../src-phoenix/phx_grid_Tet.h"
#include "../src-phoenix/phx_grid_Hex.h"
#include "../src-phoenix/phx_grid_Loadable.h"
#include "../src-phoenix/phx_grid_SerialXML.h"
#include "../src-phoenix/phx_grid_Generator.h"
#include "../src-phoenix/phx_quadrature_Element.h"
#include "../src-phoenix/phx_quadrature_Segment.h"
#include "../src-phoenix/phx_quadrature_Triangle.h"
#include "../src-phoenix/phx_quadrature_Quad.h"
#include "../src-phoenix/phx_quadrature_Tet.h"
#include "../src-phoenix/phx_quadrature_Hex.h"
#include "../src-phoenix/phx_problem_ScalarLaplacian.h"
#include "../src-phoenix/phx_viz_MEDIT.h"
#include "../src-phoenix/phx_viz_VTK.h"
#include <fstream>

using namespace phx;

double exactSolution(const char& what, const double& x, const double& y, const double& z)
{
  if (what == 'f')
    return(x);
  else if (what == 'x')
    return(1.0);
  else
    return(0.0);
}

// =========== //
// main driver //
// =========== //

int main(int argc, char *argv[])
{
#ifdef HAVE_MPI
  MPI_Init(&argc,&argv);
  Epetra_MpiComm comm(MPI_COMM_WORLD);
#else
  Epetra_SerialComm comm;
#endif

  // create a 1D grid on (0, 1) composed by segments. Each processor will have 4
  // elements. The boundary conditions of Dirichlet type and then imposed on
  // the assembled matrix.
  
  int numDimensions = 1;
  int numMyElements = 4;

  RefCountPtr<Epetra_Map> domainMap, leftCornerMap, rightCornerMap;
  
  domainMap = rcp(new Epetra_Map(-1, numMyElements, 0, comm));
  RefCountPtr<phx::grid::Loadable> domain, leftCorner, rightCorner;
  
  RefCountPtr<phx::grid::Element> domainElement, cornerElement;
  domainElement = rcp(new phx::grid::Segment(numDimensions));
  cornerElement = rcp(new phx::grid::Point(numDimensions));

  domain = rcp(new phx::grid::Loadable(domainMap, domainElement));

  int* myGlobalElements = domainMap->MyGlobalElements();

  // each processor inserts locally owned elements
  for (int LID = 0; LID < numMyElements; ++LID)
  {
    int GID = myGlobalElements[LID];

    domain->setGlobalConnectivity(GID, 0, GID);
    domain->setGlobalConnectivity(GID, 1, GID + 1);
  }

  domain->freezeConnectivity();

  double h = 1.0 / domain->getNumGlobalElements();

  int* myGlobalVertices = domain->getVertexMap()->MyGlobalElements();

  for (int LID = 0; LID < domain->getNumMyVertices(); ++LID)
  {
    int GID = myGlobalVertices[LID];
    domain->setGlobalCoordinates(GID, 0, h * GID);
  }

  domain->freezeCoordinates();

  map<string, RefCountPtr<phx::grid::Loadable> > patches;
  patches["domain"] = domain;

  // create the map for the matrix, in this case simply linear.
  RefCountPtr<Epetra_Map> matrixMap = rcp(new Epetra_Map(domain->getNumGlobalVertices(), 0, comm));

  Epetra_FECrsMatrix matrix(Copy, *matrixMap, 0);
  Epetra_FEVector    lhs(*matrixMap);
  Epetra_FEVector    rhs(*matrixMap);

  Epetra_IntSerialDenseVector vertexList(domainElement->getNumVertices());

  phx::quadrature::Segment domainQuadrature(4);
  phx::problem::ScalarLaplacian problem;
  Epetra_SerialDenseMatrix elementLHS(domainElement->getNumVertices(),domainElement->getNumVertices());
  Epetra_SerialDenseVector elementRHS(domainElement->getNumVertices());

  for (int i = 0; i < domain->getNumMyElements(); ++i)
  {
    // load the element vertex IDs
    for (int j = 0; j < domainElement->getNumVertices(); ++j)
      vertexList[j] = domain->getMyConnectivity(i, j);

    // load the element coordinates
    for (int j = 0; j < domainElement->getNumVertices(); ++j)
      for (int k = 0; k < domainElement->getNumDimensions(); ++k) 
        domainQuadrature(j, k) = domain->getGlobalCoordinates(vertexList[j], k);

    problem.integrate(domainQuadrature, elementLHS, elementRHS);

    matrix.InsertGlobalValues(vertexList, elementLHS);
    rhs.SumIntoGlobalValues(vertexList, elementRHS);
  }

  matrix.GlobalAssemble();
  rhs.GlobalAssemble();

  Teuchos::Hashtable<int, double> dirichletRows;
  dirichletRows.put(0, 0.0);
  dirichletRows.put(numMyElements * comm.NumProc() - 1, 0.0);

  for (int i = 0; i < matrix.NumMyRows(); ++i)
  {
    int GID = matrix.RowMatrixRowMap().GID(i);

    bool isDirichlet = false;
    if (dirichletRows.containsKey(GID)) isDirichlet = true;

    int* indices;
    double* values;
    int numEntries;
    matrix.ExtractMyRowView(i, numEntries, values, indices);

    if (isDirichlet)
    {
      for (int j = 0; j < numEntries; ++j)
      if (indices[j] != i) values[j] = 0.0;
      else values[j] = 1.0;
      rhs[0][i] = dirichletRows.get(GID);
    }
    else
    {
      for (int j = 0; j < numEntries; ++j)
      {
        if (indices[j] == i) continue;
        if (dirichletRows.containsKey(matrix.RowMatrixColMap().GID(indices[j]))) values[j] = 0.0;
      }
    }
  }

  lhs.PutScalar(0.0);

  AztecOO solver(&matrix, &lhs, &rhs);
  solver.SetAztecOption(AZ_solver, AZ_cg);
  solver.SetAztecOption(AZ_precond, AZ_dom_decomp);
  solver.SetAztecOption(AZ_subdomain_solve, AZ_icc);
  solver.SetAztecOption(AZ_output, 16);

  solver.Iterate(150, 1e-9);

  // now compute the norm of the solution
  
  Epetra_SerialDenseVector elementSol(domainElement->getNumVertices());
  Epetra_SerialDenseVector elementNorm(numDimensions);
  double normL2 = 0.0, semiNormH1 = 0.0;

  for (int i = 0; i < domain->getNumMyElements(); ++i)
  {
    for (int j = 0; j < domainElement->getNumVertices(); ++j)
    {
      vertexList[j] = domain->getMyConnectivity(i, j);
      elementSol[j] = lhs[0][vertexList[j]];
    }

    // load the element coordinates
    for (int j = 0; j < domainElement->getNumVertices(); ++j)
      for (int k = 0; k < domainElement->getNumDimensions(); ++k) 
        domainQuadrature(j, k) = domain->getGlobalCoordinates(vertexList[j], k);

    problem.computeNorm(domainQuadrature, exactSolution, elementNorm);

    normL2     += elementNorm[0];
    semiNormH1 += elementNorm[1];
  }

  if (comm.MyPID() == 0)
  {
    cout << "Norm L2 = " << normL2 << endl;
    cout << "SemiNorm H1 = " << semiNormH1 << endl;
  }

#ifdef HAVE_MPI
  MPI_Finalize();
#endif
}

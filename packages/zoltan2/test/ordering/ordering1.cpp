#include <Zoltan2_OrderingProblem.hpp>
#include <Zoltan2_XpetraCrsMatrixInput.hpp>
#include <Zoltan2_XpetraVectorInput.hpp>
#include <Zoltan2_TestHelpers.hpp>
#include <iostream>
#include <limits>
#include <vector>
#include <Teuchos_ParameterList.hpp>
#include <Teuchos_RCP.hpp>
#include <Teuchos_CommandLineProcessor.hpp>
#include <Tpetra_CrsMatrix.hpp>
#include <Tpetra_DefaultPlatform.hpp>
#include <Tpetra_Vector.hpp>
#include <MatrixMarket_Tpetra.hpp>

//#include <Zoltan2_Memory.hpp>  KDD User app wouldn't include our memory mgr.

#ifdef SHOW_ZOLTAN2_LINUX_MEMORY
extern "C"{
static char *meminfo=NULL;
extern void Zoltan_get_linux_meminfo(char *msg, char **result);
}
#endif

using Teuchos::RCP;
using namespace std;

/////////////////////////////////////////////////////////////////////////////
// Program to demonstrate use of Zoltan2 to order a TPetra matrix 
// (read from a MatrixMarket file or generated by MueLuGallery).
// Usage:
//     a.out [--inputFile=filename.mtx] [--outputFile=outfile.mtx] [--verbose] 
//           [--x=#] [--y=#] [--z=#] [--matrix={Laplace1D,Laplace2D,Laplace3D}
// Karen Devine, 2011
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Eventually want to use Teuchos unit tests to vary z2TestLO and
// GO.  For now, we set them at compile time based on whether Tpetra
// is built with explicit instantiation on.  (in Zoltan2_TestHelpers.hpp)

typedef lno_t z2TestLO;
typedef gno_t z2TestGO;
typedef scalar_t Scalar;

typedef Kokkos::DefaultNode::DefaultNodeType Node;
typedef Tpetra::CrsMatrix<Scalar, z2TestLO, z2TestGO> SparseMatrix;
typedef Tpetra::Vector<Scalar, z2TestLO, z2TestGO> Vector;

typedef Zoltan2::XpetraCrsMatrixInput<SparseMatrix> SparseMatrixAdapter;
typedef Zoltan2::XpetraVectorInput<Vector> VectorAdapter;

#define epsilon 0.00000001

int validatePerm(size_t n, z2TestLO *perm)
// returns 0 if permutation is valid
{
  std::vector<int> count(n);
  int status = 0;
  size_t i;

  for (i=0; i<n; i++)
    count[i]=0;

  for (i=0; i<n; i++){
    if ((perm[i]<0) || (perm[i]>=n))
      status = -1;
    else
      count[perm[i]]++;
  }

  // Each index should occur exactly once (count==1)
  for (i=0; i<n; i++){
    if (count[i] != 1){
      status = -2;
      break;
    }
  }

  return status;
}

/////////////////////////////////////////////////////////////////////////////
int main(int narg, char** arg)
{
  std::string inputFile = "";            // Matrix Market file to read
  std::string outputFile = "";           // Matrix Market file to write
  bool verbose = false;                  // Verbosity of output
  int testReturn = 0;

  ////// Establish session.
  Teuchos::GlobalMPISession mpiSession(&narg, &arg, NULL);
  RCP<const Teuchos::Comm<int> > comm =
    Tpetra::DefaultPlatform::getDefaultPlatform().getComm();
  int me = comm->getRank();

  // Read run-time options.
  Teuchos::CommandLineProcessor cmdp (false, false);
  cmdp.setOption("inputFile", &inputFile,
                 "Name of the Matrix Market sparse matrix file to read; "
                 "if not specified, a matrix will be generated by MueLu.");
  cmdp.setOption("outputFile", &outputFile,
                 "Name of the Matrix Market sparse matrix file to write, "
                 "echoing the input/generated matrix.");
  cmdp.setOption("verbose", "quiet", &verbose,
                 "Print messages and results.");
  cout << "Starting everything" << endl;

  //////////////////////////////////
  // Even with cmdp option "true", I get errors for having these
  //   arguments on the command line.  (On redsky build)
  // KDDKDD Should just be warnings, right?  Code should still work with these
  // KDDKDD params in the create-a-matrix file.  Better to have them where
  // KDDKDD they are used.
  int xdim=10;
  int ydim=10;
  int zdim=10;
  std::string matrixType("Laplace3D");

  cmdp.setOption("x", &xdim,
                "number of gridpoints in X dimension for "
                "mesh used to generate matrix.");
  cmdp.setOption("y", &ydim,
                "number of gridpoints in Y dimension for "
                "mesh used to generate matrix.");
  cmdp.setOption("z", &zdim,              
                "number of gridpoints in Z dimension for "
                "mesh used to generate matrix.");
  cmdp.setOption("matrix", &matrixType,
                "Matrix type: Laplace1D, Laplace2D, or Laplace3D");
  //////////////////////////////////

  cmdp.parse(narg, arg);


#ifdef SHOW_ZOLTAN2_LINUX_MEMORY
  if (me == 0){
    Zoltan_get_linux_meminfo("Before creating matrix", &meminfo);
    if (meminfo){
      std::cout << "Rank " << me << ": " << meminfo << std::endl;
      free(meminfo);
      meminfo=NULL;
    }
  }
#endif

  RCP<UserInputForTests> uinput;

  if (inputFile != "")  // Input file specified; read a matrix

    uinput = rcp(new UserInputForTests(inputFile, comm));

  else                  // Let MueLu generate a matrix

    uinput = rcp(new UserInputForTests(xdim, ydim, zdim, comm, matrixType));

  RCP<SparseMatrix> origMatrix = uinput->getTpetraCrsMatrix();

  if (outputFile != "") {
    // Just a sanity check.
    Tpetra::MatrixMarket::Writer<SparseMatrix>::writeSparseFile(outputFile,
                                                origMatrix, verbose);
  }

  if (me == 0) 
    cout << "NumRows     = " << origMatrix->getGlobalNumRows() << endl
         << "NumNonzeros = " << origMatrix->getGlobalNumEntries() << endl
         << "NumProcs = " << comm->getSize() << endl;

#ifdef SHOW_ZOLTAN2_LINUX_MEMORY
  if (me == 0){
    Zoltan_get_linux_meminfo("After creating matrix", &meminfo);
    if (meminfo){
      std::cout << "Rank " << me << ": " << meminfo << std::endl;
      free(meminfo);
      meminfo=NULL;
    }
  }
#endif

  ////// Create a vector to use with the matrix.
  RCP<Vector> origVector, origProd;
  origProd   = Tpetra::createVector<Scalar,z2TestLO,z2TestGO>(
                                    origMatrix->getRangeMap());
  origVector = Tpetra::createVector<Scalar,z2TestLO,z2TestGO>(
                                    origMatrix->getDomainMap());
  origVector->randomize();

  ////// Specify problem parameters
  Teuchos::ParameterList params;
  // params.set("ORDER_METHOD", "RCM");

  ////// Create an input adapter for the Tpetra matrix.
  SparseMatrixAdapter adapter(origMatrix);

  ////// Create and solve ordering problem
  Zoltan2::OrderingProblem<SparseMatrixAdapter> problem(&adapter, &params);

#ifdef SHOW_ZOLTAN2_LINUX_MEMORY
  if (me == 0){
    Zoltan_get_linux_meminfo("After creating problem", &meminfo);
    if (meminfo){
      std::cout << "Rank " << me << ": " << meminfo << std::endl;
      free(meminfo);
      meminfo=NULL;
    }
  }
#endif

  cout << "Going to solve" << endl;
  problem.solve();

  ////// Basic metric checking of the ordering solution
  size_t checkLength;
  size_t dummy;
  z2TestGO *checkGIDs;
  z2TestLO *checkPerm;
  Zoltan2::OrderingSolution<z2TestGO, z2TestLO> *soln = problem.getSolution();

  cout << "Going to get results" << endl;
  // Check that the solution is really a permutation
  checkLength = soln->getPermutationSize();
  checkGIDs = soln->getGids(&dummy);
  checkPerm = soln->getPermutation(&dummy);

  cout << "Going to validate the soln" << endl;
  // Verify that checkPerm is a permutation
  testReturn = validatePerm(checkLength, checkPerm);

  if (me == 0) {
    if (testReturn)
      std::cout << "Solution is not a permutation; FAIL" << std::endl;
    else
      std::cout << "PASS" << std::endl;
  }
}


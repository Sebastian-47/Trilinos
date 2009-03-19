#ifndef __tStridedEpetraOperator_hpp__
#define __tStridedEpetraOperator_hpp__

// Teuchos includes
#include "Teuchos_RCP.hpp"

// Epetra includes
#include "Epetra_Map.h"
#include "Epetra_CrsMatrix.h"
#include "Epetra_Vector.h"

#include <string>

#include "Test_Utils.hpp"

namespace PB {
namespace Test {

class tStridedEpetraOperator : public UnitTest {
public:
   virtual ~tStridedEpetraOperator() {}

   virtual void initializeTest();
   virtual int runTest(int verbosity,std::ostream & stdstrm,std::ostream & failstrm,int & totalrun);
   virtual bool isParallel() const { return true; }

   bool test_numvars_constr(int verbosity,std::ostream & os);
   bool test_vector_constr(int verbosity,std::ostream & os);

protected:
   double tolerance_;
};

} // end namespace Tests
} // end namespace PB

#endif

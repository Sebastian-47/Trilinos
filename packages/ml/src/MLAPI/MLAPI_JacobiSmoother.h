#ifndef ML_JACOBISMOOTHER_H
#define ML_JACOBISMOOTHER_H
#include "ml_config.h"
#include <iostream>
#include "ml_smoother.h"
#include "ml_epetra.h"
#include "ml_amesos.h"
#include "ml_amesos_wrap.h"
#include "MLAPI_Space.h"
#include "MLAPI_Vector.h"
#include "MLAPI_Smoother.h"

using namespace std;

namespace MLAPI {

class JacobiSmoother : public Smoother {

public:

  JacobiSmoother(const Operator& Matrix,
              Teuchos::ParameterList& List) :
    ml_handle_(0),
    Smoother_(0),
    ApplyInverseTemp_(0)
  {
    RangeSize_ = Matrix.RangeSpace().NumMyElements();
    RangeSpace_ = new Space(Matrix.RangeSpace());
    DomainSize_ = Matrix.DomainSpace().NumMyElements();
    DomainSpace_ = new Space(Matrix.DomainSpace());
    ApplyInverseTemp_ = new Vector(*RangeSpace_);

    // build symmetric Gauss-Seidel on ml_handle_
    ML_Create(&ml_handle_,1);
    memcpy(ml_handle_->Amat,Matrix.GetOperator(),sizeof(ML_Operator));
    ML_Gen_Smoother_Jacobi(ml_handle_,0,ML_POSTSMOOTHER,1,1.0);
    Smoother_ = ml_handle_->SingleLevel[0].post_smoother;
  }

  ~JacobiSmoother()
  {
    if (ml_handle_)
      ML_Destroy(&ml_handle_);

    if (ApplyInverseTemp_)
      delete ApplyInverseTemp_;
  }

  int ApplyInverse(const Vector& lhs, Vector& rhs) const
  {
    if (Smoother_ == 0)
      throw("Smoother not set");

    ML_Smoother_Apply(Smoother_, (int)RangeSize_, (double*)&rhs[0], 
                      (int)DomainSize_, (double*)&lhs[0], ML_NONZERO);
  }

  Vector& ApplyInverse(const Vector& lhs) const
  {
    (*ApplyInverseTemp_) = 0;
    ApplyInverse(lhs,*ApplyInverseTemp_);
    return(*ApplyInverseTemp_);
  }

  Space& RangeSpace() const {
    return(*RangeSpace_);
  }

  Space& DomainSpace() const {
    return(*DomainSpace_);
  }

private:
  int RangeSize_;
  int DomainSize_;
  Space* DomainSpace_;
  Space* RangeSpace_;
  ML_Smoother* Smoother_;
  ML* ml_handle_;
  mutable Vector* ApplyInverseTemp_;
}; // JacobiSmoother

} // namespace MLAPI
#endif // ML_JACOBISMOOTHER_H

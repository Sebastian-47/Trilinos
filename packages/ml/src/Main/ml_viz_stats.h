/********************************************************************* */
/* See the file COPYRIGHT for a complete copyright notice, contact      */
/* person and disclaimer.                                               */   
/* ******************************************************************** */

#ifndef __MLVIZSTATS__
#define __MLVIZSTATS__

/*MS*/
#define ML_AGGREGATE_VIZ_STATS_ID 24680

typedef struct ML_Aggregate_Viz_Stats_Struct
{
  int id;
  double *x;
  double *y;
  double *z;
  int Ndim;
  int *graph_decomposition;
  int Nlocal;
  int Naggregates;
  int local_or_global;
  int is_filled;
  int MaxNodesPerAgg;
  void *Amatrix;  /* void * so that I do not have to include
		     ml_operator.h */
  
} ML_Aggregate_Viz_Stats;
/*ms*/

#endif /* #ifndef __MLAGGMETIS__ */

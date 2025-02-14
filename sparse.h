#ifndef SPARSE_H
#define SPARSE_H

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "hilbert.h"
#include "quickSort.h"
#include "utils.h"

struct SparseBinaryMatrix
{
  int nrow;
  int ncol;
  long nnz;
  int* rows;
  int* cols;
};

/** constructor, computes nrow and ncol from data */
static inline struct SparseBinaryMatrix* new_sbm(long nrow, long ncol, long nnz, int* rows, int* cols) {
  struct SparseBinaryMatrix *A = (struct SparseBinaryMatrix*)malloc(sizeof(struct SparseBinaryMatrix));
  A->nnz  = nnz;
  A->rows = rows;
  A->cols = cols;
  A->nrow = nrow;
  A->ncol = ncol;
  return A;
}

static inline void free_sbm(struct SparseBinaryMatrix* sbm) {
  free( sbm->rows );
  free( sbm->cols );
}



static inline struct SparseBinaryMatrix* new_transpose(struct SparseBinaryMatrix *A) {
  struct SparseBinaryMatrix *B = (struct SparseBinaryMatrix*)malloc(sizeof(struct SparseBinaryMatrix));
  B->nnz  = A->nnz;
  B->rows = A->cols;
  B->cols = A->rows;
  B->nrow = A->ncol;
  B->ncol = A->nrow;
  return B;
}

static inline void transpose(struct SparseBinaryMatrix *A) {
  int* tmp = A->rows;
  A->rows = A->cols;
  A->cols = tmp;
  int ntmp = A->nrow;
  A->nrow = A->ncol;
  A->ncol = ntmp;
}

/** y = A * x */
static inline void A_mul_B(double* y, struct SparseBinaryMatrix *A, double* x) {
  int* rows = A->rows;
  int* cols = A->cols;
  memset(y, 0, A->nrow * sizeof(double));
  for (long j = 0; j < A->nnz; j++) {
    y[rows[j]] += x[cols[j]];
  }
}

/** y = A' * x */
static inline void At_mul_B(double* y, struct SparseBinaryMatrix *A, double* x) {
  int* rows = A->rows;
  int* cols = A->cols;
  memset(y, 0, A->ncol * sizeof(double));
  for (long j = 0; j < A->nnz; j++) {
    y[cols[j]] += x[rows[j]];
  }
}

static inline double exprand() {
  double x = 1 - drand48();
  return log1p(x);

}

static inline double randexp() {
  return -log(1 - drand48());
}

/** computes random set of samples:
 *  @input N          range from 0 to N-1
 *  @output samples   array for storing sampled values
 *  @returns          number of sampled values
 **/
static inline long randsubseq(long N, long max_samples, double p, long* samples) {
  double L = -1.0 / log1p(-p);
  long i = -1;
  long j = 0;

  while (1) {
    double s = randexp() * L;
    if (s + i >= N - 1) {
      return j;
    }
    i += (long)ceil(s);
    // adding j-th sample:
    samples[j] = i;
    j++;
    if (j >= max_samples) {
      return j;
    }
  }
}

static inline struct SparseBinaryMatrix* read_sbm(const char *filename) {
  FILE* fh = fopen( filename, "r" );
  size_t result1, result2;
  if (fh == NULL) {
    fprintf( stderr, "File error: %s\n", filename );
    exit(1);
  }
  long nrow = read_long(fh);
  long ncol = read_long(fh);
  long nnz  = read_long(fh);
  // reading data
  int* rows = (int*)malloc(nnz * sizeof(int));
  int* cols = (int*)malloc(nnz * sizeof(int));
  result1 = fread(rows, sizeof(int), nnz, fh);
  result2 = fread(cols, sizeof(int), nnz, fh);
  if ((int)result1 != nnz || (int)result2 != nnz) {
    fprintf( stderr, "File read error: %s\n", filename );
    exit(1);
  }
  fclose(fh);
  // convert data from 1 based to 0 based
  for (long i = 0; i < nnz; i++) {
    rows[i]--;
    cols[i]--;
  }

  return new_sbm(nrow, ncol, nnz, rows, cols);
}

/** sorts SBM according to Hilbert curve */
static inline void sort_sbm(struct SparseBinaryMatrix *A) {
  int* rows = A->rows;
  int* cols = A->cols;

  int maxrc = A->nrow > A->ncol ? A->nrow : A->ncol;
  int n = ceilPower2(maxrc);

  long* h = (long*)malloc(A->nnz * sizeof(long));
#pragma omp parallel for schedule(static)
  for (long j = 0; j < A->nnz; j++) {
    h[j] = xy2d(n, rows[j], cols[j]);
  }
  quickSort(h, 0, A->nnz - 1);
#pragma omp parallel for schedule(static)
  for (long j = 0; j < A->nnz; j++) {
    d2xy(n, h[j], &rows[j], &cols[j]);
  }

  free(h);
}

struct BlockedSBM {
  int nrow;
  int ncol;
  /** row blocks */
  int nblocks;
  int* start_row;
  int* nnz;
  int** rows;
  int** cols;
};

/** constructor for blocked rows */
static inline struct BlockedSBM* new_bsbm(struct SparseBinaryMatrix* A, int block_size) {
  struct BlockedSBM *B = (struct BlockedSBM*)malloc(sizeof(struct BlockedSBM));
  B->nrow = A->nrow;
  B->ncol = A->ncol;
  B->nblocks = (int)ceil(A->nrow / (double)block_size);
  B->nnz     = (int*)malloc(B->nblocks * sizeof(int));

  // array of starting rows, including the last
  B->start_row = (int*)malloc((B->nblocks + 1) * sizeof(int));
  for (int i = 0; i < B->nblocks; i++) {
    B->start_row[i] = i * block_size;
    B->nnz[i] = 0;
  }
  B->start_row[B->nblocks] = B->nrow;

  B->rows = (int**)malloc(B->nblocks * sizeof(int*));
  B->cols = (int**)malloc(B->nblocks * sizeof(int*));

  // counting nnz in each block
  for (long j = 0; j < A->nnz; j++) {
    int block = A->rows[j] / block_size;
    B->nnz[block]++;
  }
  int* bcounts = (int*)malloc(B->nblocks * sizeof(int));
  for (int i = 0; i < B->nblocks; i++) {
    bcounts[i] = 0;
    B->rows[i] = (int*)malloc(B->nnz[i] * sizeof(int));
    B->cols[i] = (int*)malloc(B->nnz[i] * sizeof(int));
  }

  for (long j = 0; j < A->nnz; j++) {
    int block = A->rows[j] / block_size;
    B->rows[block][bcounts[block]] = A->rows[j];
    B->cols[block][bcounts[block]] = A->cols[j];
    bcounts[block]++;
  }

  return B;
}

static inline void sort_bsbm(struct BlockedSBM *B) {
  for (int block = 0; block < B->nblocks; block++) {
    int* rows = B->rows[block];
    int* cols = B->cols[block];
    int nnz = B->nnz[block];
    int start_row = B->start_row[block];
    int n = ceilPower2(B->start_row[block+1] - B->start_row[block]);

    // convert to hilbert, sort, convert back
    long* h = (long*)malloc(nnz * sizeof(long));
    for (long j = 0; j < nnz; j++) {
      h[j] = row_xy2d(n, rows[j] - start_row, cols[j]);
    }
    quickSort(h, 0, nnz - 1);
    for (long j = 0; j < nnz; j++) {
      row_d2xy(n, h[j], &rows[j], &cols[j]);
      rows[j] += start_row;
    }

    free(h);
  }
}

static inline void sort_bsbm_byrow(struct BlockedSBM *B) {
  for (int block = 0; block < B->nblocks; block++) {
    int* rows = B->rows[block];
    int* cols = B->cols[block];
    int nnz = B->nnz[block];

    long* h = (long*)malloc(nnz * sizeof(long));
    for (long j = 0; j < nnz; j++) {
      h[j] = rows[j] * (long)B->ncol + (long)cols[j];
    }
    quickSort(h, 0, nnz - 1);
    for (long j = 0; j < nnz; j++) {
      rows[j] = h[j] / B->ncol;
      cols[j] = h[j] % B->ncol;
    }

    free(h);
  }
}

/** y = B * x */
static inline void bsbm_A_mul_B(double* y, struct BlockedSBM *B, double* x) {
#pragma omp parallel for schedule(dynamic, 1)
  for (int block = 0; block < B->nblocks; block++) {
    int* rows = B->rows[block];
    int* cols = B->cols[block];
    int nnz = B->nnz[block];

    // zeroing y:
    memset(y + B->start_row[block], 0, (B->start_row[block+1] - B->start_row[block]) * sizeof(double));

    for (int j = 0; j < nnz; j++) {
      y[rows[j]] += x[cols[j]];
    }
  }
}

/** Y = B * X, where X has 2 cols, Y and X are row-ordered */
static inline void bsbm_A_mul_B2(double* y, struct BlockedSBM *B, double* x) {
#pragma omp parallel for schedule(dynamic, 1)
  for (int block = 0; block < B->nblocks; block++) {
    int* rows = B->rows[block];
    int* cols = B->cols[block];
    int nnz = B->nnz[block];

    // zeroing Y:
    memset(y + 2 * B->start_row[block], 0, 2 * (B->start_row[block+1] - B->start_row[block]) * sizeof(double));

    for (int j = 0; j < nnz; j++) {
      int row = rows[j] * 2;
      int col = cols[j] * 2;
      y[row++] += x[col++];
      y[row]   += x[col];
    }
  }
}

/** Y = B * X, where X has 4 cols, Y and X are row-ordered */
static inline void bsbm_A_mul_B4(double* y, struct BlockedSBM *B, double* x) {
#pragma omp parallel for schedule(dynamic, 1)
  for (int block = 0; block < B->nblocks; block++) {
    int* rows = B->rows[block];
    int* cols = B->cols[block];
    int nnz = B->nnz[block];

    // zeroing Y:
    memset(y + 4 * B->start_row[block], 0, 4 * (B->start_row[block+1] - B->start_row[block]) * sizeof(double));

    for (int j = 0; j < nnz; j++) {
      int row = rows[j] * 4;
      int col = cols[j] * 4;
      y[row++] += x[col++];
      y[row++] += x[col++];
      y[row++] += x[col++];
      y[row]   += x[col];
    }
  }
}

/** Y = B * X, where X has ncol columns, Y and X are row-ordered */
static inline void bsbm_A_mul_Bn(double* y, struct BlockedSBM *B, double* x, int ncol) {
#pragma omp parallel for schedule(dynamic, 1)
  for (int block = 0; block < B->nblocks; block++) {
    int* rows = B->rows[block];
    int* cols = B->cols[block];
    int nnz = B->nnz[block];

    // zeroing Y:
    memset(y + ncol * B->start_row[block], 0, ncol * (B->start_row[block+1] - B->start_row[block]) * sizeof(double));

    for (int j = 0; j < nnz; j++) {
      int row = rows[j] * ncol;
      int col = cols[j] * ncol;
      for (int k = 0; k < ncol; k++) {
         y[row++] += x[col++];
      }
    }
  }
}
#endif /* SPARSE_H */

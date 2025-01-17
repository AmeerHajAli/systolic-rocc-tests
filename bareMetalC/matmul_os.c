// See LICENSE for license details.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "include/systolic.h"

#define N (2)

void operands(int c, int * a, int * b, int * d) {
  *d = c % N;
  *b = (c / N) % N;
  *a = c / (N*N);
}

#if (3*N + N*N*N)*DIM > (BANK_NUM * BANK_ROWS)
#error scratchpad not big enough
#endif

int main() {
  static elem_t ZERO[DIM][DIM];

  for (int activation = 0; activation <= 2; ++activation) {
    for (int shift = 0; shift <= 12; shift += 4) {
      // printf("activation: %d, shift: %d\n", activation, shift);

      static elem_t A[N][DIM][DIM] row_align(1);
      static elem_t B[N][DIM][DIM] row_align(1);
      static elem_t D[N][DIM][DIM] row_align(1);

      int relu6_shift = shift+1;

      // We will try out every combination of A, B, D possible
      static elem_t C[N*N*N][DIM][DIM] row_align(1);
      static int64_t gold_full[N*N*N][DIM][DIM];
      static elem_t gold[N*N*N][DIM][DIM];

      // ...taking into account the preloads or accumulates
      static int preload[N*N*N] = {1};
      for (int i = 1; i < N*N*N; ++i)
        preload[i] = rand() % 2;

      // ...and for the actual preloads, do we just preload zeros?
      static int preload_zeros[N*N*N];
      for (int i = 0; i < N*N*N; ++i)
        preload_zeros[i] = rand() % 2;

      // ...and finally, which results won't produce any output
      static int no_output[N*N*N];
      for (int i = 0; i < N*N*N-1; ++i)
        no_output[i] = !preload[i+1];
      no_output[N*N*N-1] = 0;

      // Print the sequence out
      /*printf("Preloads: ");
      for (int i = 0; i < N*N*N; ++i)
        printf("%d, ", preload[i]);
      printf("\n");
      printf("Zeros: ");
      for (int i = 0; i < N*N*N; ++i)
        printf("%d, ", preload_zeros[i]);
      printf("\n");
      printf("No outputs: ");
      for (int i = 0; i < N*N*N; ++i)
        printf("%d, ", no_output[i]);
      printf("\n");*/

      for (size_t n = 0; n < N; ++n) {
        for (size_t i = 0; i < DIM; ++i) {
          for (size_t j = 0; j < DIM; ++j) {
            A[n][i][j] = (rand() % 64) - 32;
            B[n][i][j] = (rand() % 64) - 32;
            D[n][i][j] = (rand() % 64) - 32;
          }
        }
      }

      for (size_t g = 0; g < N*N*N; ++g) {
        int a, b, d; 
        operands(g, &a, &b, &d);

        if (!preload[g])
          matmul_full(A[a], B[b], gold_full[g-1], gold_full[g]);
        else if (preload_zeros[g])
          matmul(A[a], B[b], ZERO, gold_full[g]);
        else
          matmul(A[a], B[b], D[d], gold_full[g]);
      }

      for (size_t g = 0; g < N*N*N; ++g) {
          matshift(gold_full[g], gold[g], shift);
          if (activation == RELU)
            matrelu(gold[g], gold[g]);
          else if (activation == RELU6)
            matrelu6(gold[g], gold[g], 1 << relu6_shift);
      }

      int A_addr = 0;
      int B_addr = N*DIM;
      int D_addr = 2*N*DIM;
      int C_addr = 3*N*DIM;

      // printf("Moving in\n");
      for (size_t n = 0; n < N; ++n)
        matmul_mvin(A[n], A_addr + n*DIM, 0, 0, 0, 0);
      
      for (size_t n = 0; n < N; ++n)
        matmul_mvin(B[n], B_addr + n*DIM, 0, 0, 0, 0);

      for (size_t n = 0; n < N; ++n) {
        if (n == N-1) {
          matmul_mvin(D[n], D_addr + n*DIM, 0, 0, 1, 0);
        } else {
          matmul_mvin(D[n], D_addr + n*DIM, 0, 0, 0, 0);
        }
      }

      // printf("Setting mode\n");
      matmul_config_ex(OUTPUT_STATIONARY, activation, shift, relu6_shift, 0, 1, 0, 0);

      // printf("Matmulling\n");
      for (size_t c = 0; c < N*N*N; ++c) {
        int a, b, d;
        operands(c, &a, &b, &d);
        
        uint64_t out_addr = C_addr + c*DIM;
        if (no_output[c])
          out_addr = GARBAGE_ADDR;

        if (!preload[c]) {
          if (c == N*N*N-1) {
            matmul_preload_zeros(out_addr, 0, 0, 1, 0);
          } else {
            matmul_preload_zeros(out_addr, 0, 0, 0, 0);
          }
          matmul_compute_accumulated(A_addr + a*DIM, B_addr + b*DIM);
        } else if (preload_zeros[c]) {
          if (c == N*N*N-1) {
            matmul_preload_zeros(out_addr, 0, 0, 1, 0);
          } else {
            matmul_preload_zeros(out_addr, 0, 0, 0, 0);
          }
          matmul_compute_preloaded(A_addr + a*DIM, B_addr + b*DIM);
        } else {
          if (c == N*N*N-1) {
            matmul_preload(D_addr + d*DIM, out_addr, 0, 0, 1, 0);
          } else {
            matmul_preload(D_addr + d*DIM, out_addr, 0, 0, 0, 0);
          }
          matmul_compute_preloaded(A_addr + a*DIM, B_addr + b*DIM);
        }
      }

      matmul_fence();
      // printf("Moving out\n");
      int first_store = 1;
      for (size_t c = 0; c < N*N*N; ++c)
        if (!no_output[c]) {
          //printf("%x\n", &C[c][0]);
          //if ((int)(C[c]) % (DIM*sizeof(elem_t)) != 0) {
          //    // printf("%x\n", C[c]);
          //    exit(1);
          //}
          if (first_store) {
            matmul_mvout(&C[c][0][0], C_addr + c*DIM, 0, 0, 0, 1);
            first_store = 0;
          } else {
            matmul_mvout(&C[c][0][0], C_addr + c*DIM, 0, 0, 0, 0);
          }
        }

      matmul_fence();

      /*printf("Moved out\n");
      for (int n = 0; n < N*N*N; ++n) {
        if (!no_output[n]) {
          printf("C:\n");
          printMatrix(C[n]);
          printf("Gold:\n");
          printMatrix(gold[n]);
          printf("\n");
        }
      }*/

      for (int n = 0; n < N*N*N; ++n)
        if (!no_output[n] && !is_equal(C[n], gold[n])) {
            printf("activation: %d, shift: %d\n", activation, shift);

            printf("C:\n");
            printMatrix(C[n]);
            printf("Gold:\n");
            printMatrix(gold[n]);
            printf("Gold_full:\n");
            for (size_t i = 0; i < DIM; ++i) {
                for (size_t j = 0; j < DIM; ++j) {
                    printf("%lld ", gold_full[n][i][j]);
                }
                printf("\n");
            }
            printf("\n");

            exit(1);
        }
    }
  }

  exit(0);
}


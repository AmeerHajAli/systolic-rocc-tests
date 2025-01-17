// See LICENSE for license details.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "include/systolic.h"

#define N 8

#if (N*DIM) > ACC_ROWS
#error not enough accumulator space
#endif

int main() {
  for (int activation = 0; activation <= 2; ++activation) {
    for (int shift = 0; shift <= 12; shift += 4) {
      // printf("activation: %d, shift: %d\n", activation, shift);

      static acc_t In[N][DIM][DIM] row_align_acc(1);
      static int64_t In_full[N][DIM][DIM];
      static elem_t Out[N][DIM][DIM] row_align(1);
      static elem_t Out_gold[N][DIM][DIM];

      int relu6_shift = shift+1;

      // printf("Initializing matrices\n");
      for (size_t n = 0; n < N; ++n)
        for (size_t i = 0; i < DIM; ++i)
          for (size_t j = 0; j < DIM; ++j) {
            In[n][i][j] = 0;

            int bytes = rand() % 2 ? sizeof(acc_t) : sizeof(elem_t);
            for (size_t b = 0; b < bytes; ++b) {
              In[n][i][j] |= (rand() % 255) << (b*8);
            }

            In_full[n][i][j] = In[n][i][j];
          }

      // printf("Shifting and activating matrices\n");
      for (size_t n = 0; n < N; ++n) {
        matshift(In_full[n], Out_gold[n], shift);

        if (activation == RELU)
          matrelu(Out_gold[n], Out_gold[n]);
        else if (activation == RELU6)
          matrelu6(Out_gold[n], Out_gold[n], 1 << relu6_shift);
      }

      const uint32_t acc_addr = 1 << (ADDR_LEN-1);

      // printf("Config\n");
      matmul_config_ld(DIM*sizeof(acc_t), 0, 0, 0, 0);
      matmul_config_ex(0, activation, shift, relu6_shift, 0, 0, 1, 0);
      matmul_config_st(DIM*sizeof(elem_t), 0, 0, 0, 1);

      for (size_t n = 0; n < N; ++n) {
        // printf("n: %u\n", n);
        matmul_mvin(In[n], acc_addr + n*DIM, 1, 0, 0, 0);
        matmul_mvout(Out[n], acc_addr + n*DIM, 0, 1, 0, 0);
      }

      matmul_fence();

      for (size_t n = 0; n < N; ++n)
        if (!is_equal(Out[n], Out_gold[n])) {
          printf("activation: %d, shift: %d\n", activation, shift);

          printf("Matrix %u:\n", n);
          for (size_t i = 0; i < DIM; ++i) {
            for (size_t j = 0; j < DIM; ++j)
              printf("%d ", In[n][i][j]);
            printf("\n");
          }
          printf("Matrix %u output:\n", n);
          printMatrix(Out[n]);
          printf("Matrix %u gold output:\n", n);
          printMatrix(Out_gold[n]);
          printf("\n");

          exit(1);
        }
    }
  }

  exit(0);
}


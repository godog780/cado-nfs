#include "cado.h"
#include <pthread.h>
#include <string.h>
#include "tests_common.h"
#include "thread.h"

class print_parameter : public task_parameters {
public:
  const char *msg;
  print_parameter(const char *m) : msg(m){}
};

class print_result : public task_result {
public:
  int printed;
  print_result(int n) : printed(n){}
};

task_result *print_something(const task_parameters *t_param)
{
  const print_parameter *param = static_cast<const print_parameter *>(t_param);

  pthread_t tid = pthread_self();
  unsigned int tid_u = 0;
  memcpy(&tid_u, &tid, MIN(sizeof(tid), sizeof(tid_u)));

  int rc = printf("This is thread %u: %s", tid_u, param->msg);
  return new print_result(rc);
}

int main(int argc, const char **argv)
{
  tests_common_cmdline(&argc, &argv, PARSE_ITER);
  unsigned long iter = 10;
  tests_common_get_iter(&iter);

  thread_pool *pool = new thread_pool(5);

  print_parameter param("Hello world!\n");

  for (unsigned long i = 0; i < iter; i++) {
    pool->add_task(print_something, &param, 1);
  }

  for (unsigned long i = 0; i < iter; i++) {
    print_result *result = static_cast<print_result *>(pool->get_result());
    printf("I've printed %d characters\n", result->printed);
    delete result;
  }

  delete pool;
  return 0;
}

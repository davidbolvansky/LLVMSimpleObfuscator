
int fact(int n) {
  if(n == 0)
    return 1;
  int res = n * fact(n-1);
  return res;
}

int main(int argc, char **argv) {
   return fact(argc + 4);
}
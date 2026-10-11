/* Standalone entry point for ghostlock exploit */
extern int run_exploit(int initial, char *const argv[]);
int main(int argc, char *argv[]) {
    return run_exploit(1, argv);
}

/* find_prime.c */

/* naive program to find the next prime number larger than N */

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

void usage()
{
    printf("usage: find_prime N\n\n");
    printf("\tfinds the next prime larger than N, where N is a positive integer\n");
    printf("\tprobably only useful for relatively small N\n\n");
}

#define IS_DIVISIBLE_BY(n,m) (((n)%(m))==0)

/* return true if n is prime */
int is_prime(int n)
{
    int i;

    /* see if even, if so its false */
    if (IS_DIVISIBLE_BY(n,2))
        return 0;

    /* see if it is divisible by any odd integer between 3 and n/2 */
    for (i=3; i < n/2; i += 2)
    {
        if (IS_DIVISIBLE_BY(n,i))
            return 0;
    }

    return 1;
}


int main(int argc, char** argv)
{
    int i, n;

    if (argc == 1)
    {
        usage();
        return 1;
    }

    n = atoi(argv[1]);
    if (n <= 1)
    {
        usage();
        return 1;
    }

    for (i = n; i < INT_MAX-1; i++)
    {
        if (is_prime(i))
        {
            printf("%d\n",i);
            break;
        }
    }

    return 0;
}

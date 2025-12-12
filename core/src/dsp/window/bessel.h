namespace dsp::window {
    // Modified Bessel function of the first kind, order 0
    inline double besselI0(double x) {
        double sum = 1.0;
        double y = x * x / 4.0;
        double t = y;
        for (int k = 1; k < 32; k++) {
            sum += t;
            t *= y / (double)((k + 1) * (k + 1));
        }
        return sum;
    }
}

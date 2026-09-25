static const float int16ToFloat = 1.0f / 32768.0f;
static const float int24ToFloat = 1.0f / 8388608.0f;
static const float int32ToFloat = 1.0f / 2147483648.0f;

// DSD is not supported

static void toFloat(const void *input, float *output, int count, int step, ASIOSampleType sampleType) {
    switch (sampleType) {
        case ASIOSTInt16LSB: {
            const short int *in = (const short int *)input;
            while (count--) { *output = *in++ * int16ToFloat; output += step; }
        } break;
        case ASIOSTInt32LSB16: {
            const short int *in = (const short int *)input;
            while (count--) { *output = *in * int16ToFloat; output += step; in += 2; }
        } break;
        case ASIOSTInt24LSB: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                int i = (in[2] << 16) | (in[1] << 8) | in[0]; in += 3;
                if (i & 0x800000) i |= 0xFF000000;
                *output = i * int24ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32LSB18: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                unsigned int i = (in[2] << 16) | (in[1] << 8) | in[0]; in += 4;
                *output = (int)(i << 14) * int32ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32LSB20: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                unsigned int i = (in[2] << 16) | (in[1] << 8) | in[0]; in += 4;
                *output = (int)(i << 12) * int32ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32LSB24: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                unsigned int i = (in[2] << 16) | (in[1] << 8) | in[0]; in += 4;
                *output = (int)(i << 8) * int32ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32LSB: {
            const int *in = (const int *)input;
            while (count--) { *output = *in++ * int32ToFloat; output += step; }
        } break;

        case ASIOSTInt16MSB: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                union { short int i; unsigned char u[2]; } v;
                v.u[1] = in[0]; v.u[0] = in[1]; in += 2;
                *output = v.i * int16ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32MSB16: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                unsigned int i = (in[2] << 8) | in[3]; in += 4;
                *output = (int)(i << 16) * int32ToFloat; output += step;
            }
        } break;
        case ASIOSTInt24MSB: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                int i = (in[0] << 16) | (in[1] << 8) | in[2]; in += 3;
                if (i & 0x800000) i |= 0xFF000000;
                *output = i * int24ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32MSB18: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                unsigned int i = (in[1] << 16) | (in[2] << 8) | in[3]; in += 4;
                *output = (int)(i << 14) * int32ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32MSB20: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                unsigned int i = (in[1] << 16) | (in[2] << 8) | in[3]; in += 4;
                *output = (int)(i << 12) * int32ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32MSB24: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                unsigned int i = (in[1] << 16) | (in[2] << 8) | in[3]; in += 4;
                *output = (int)(i << 8) * int32ToFloat; output += step;
            }
        } break;
        case ASIOSTInt32MSB: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                union { int i; unsigned char u[4]; } v;
                v.u[3] = in[0]; v.u[2] = in[1]; v.u[1] = in[2]; v.u[0] = in[3]; in += 4;
                *output = v.i * int32ToFloat; output += step;
            }
        } break;

        case ASIOSTFloat32LSB: {
            const float *in = (const float *)input;
            while (count--) { *output = *in++; output += step; }
        } break;
        case ASIOSTFloat64LSB: {
            const double *in = (const double *)input;
            while (count--) { *output = (float)*in++; output += step; }
        } break;

        case ASIOSTFloat32MSB: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                union { float f; unsigned char u[4]; } v;
                v.u[3] = in[0]; v.u[2] = in[1]; v.u[1] = in[2]; v.u[0] = in[3]; in += 4;
                *output = v.f; output += step;
            }
        } break;
        case ASIOSTFloat64MSB: {
            const unsigned char *in = (const unsigned char *)input;
            while (count--) {
                union { double d; unsigned char u[8]; } v;
                v.u[7] = in[0]; v.u[6] = in[1]; v.u[5] = in[2]; v.u[4] = in[3]; 
                v.u[3] = in[4]; v.u[2] = in[5]; v.u[1] = in[6]; v.u[0] = in[7]; in += 8;
                *output = (float)v.d; output += step;
            }
        } break;
        default:
            if (step == 1) memset(output, 0, count * sizeof(float));
            else while (count--) { *output = 0.0f; output += step; }
            break;
    }
}

static void zero(void *buf, int count, ASIOSampleType sampleType) {
    switch (sampleType) {
        case ASIOSTInt16LSB:
        case ASIOSTInt16MSB: memset(buf, 0, count * 2); break;
        case ASIOSTInt24LSB:
        case ASIOSTInt24MSB: memset(buf, 0, count * 3); break;
        case ASIOSTInt32LSB:
        case ASIOSTInt32LSB16:
        case ASIOSTInt32LSB18:
        case ASIOSTInt32LSB20:
        case ASIOSTInt32LSB24:
        case ASIOSTInt32MSB:
        case ASIOSTInt32MSB16:
        case ASIOSTInt32MSB18:
        case ASIOSTInt32MSB20:
        case ASIOSTInt32MSB24:
        case ASIOSTFloat32LSB:
        case ASIOSTFloat32MSB: memset(buf, 0, count * 4); break;
        case ASIOSTFloat64LSB:
        case ASIOSTFloat64MSB: memset(buf, 0, count * 8); break;
        default: return;
    }
}

static void clamp(float *f, int count, int step, ASIOSampleType sampleType) {
    switch (sampleType) {
        case ASIOSTFloat32LSB:
        case ASIOSTFloat32MSB:
        case ASIOSTFloat64LSB:
        case ASIOSTFloat64MSB: return;
    }
    count *= step;
    for (int n = 0; n < count; n += step) f[n] = fmaxf(-1.0f, fminf(1.0f, f[n]));
}

static void fromFloat(float *input, void *output, int count, int step, ASIOSampleType sampleType) {
    clamp(input, count, step, sampleType);
    switch (sampleType) {
        case ASIOSTInt16LSB: {
            short int *out = (short int *)output;
            while (count--) { *out++ = (short int)(*input * 32767.0f); input += step; }
        } break;
         case ASIOSTInt32LSB16: {
            short int *out = (short int *)output;
            while (count--) { *out++ = (short int)(*input * 32767.0f); input += step; *out++ = 0; }
        } break;
        case ASIOSTInt24LSB: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                int v = (int)(*input * 8388607.0f); input += step;
                out[0] = v & 0xff; out[1] = (v >> 8) & 0xff; out[2] = (v >> 16) & 0xff; out += 3;
            }
        } break;
        case ASIOSTInt32LSB18: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                unsigned int v = (unsigned int)(int)(*input * 2147483520.0f) >> 14; input += step;
                out[0] = v & 0xff; out[1] = (v >> 8) & 0xff; out[2] = (v >> 16) & 0xff; out[3] = 0; out += 4;
            }
        } break;
        case ASIOSTInt32LSB20: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                unsigned int v = (unsigned int)(int)(*input * 2147483520.0f) >> 12; input += step;
                out[0] = v & 0xff; out[1] = (v >> 8) & 0xff; out[2] = (v >> 16) & 0xff; out[3] = 0; out += 4;
            }
        } break;
        case ASIOSTInt32LSB24: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                unsigned int v = (unsigned int)(int)(*input * 2147483520.0f) >> 8; input += step;
                out[0] = v & 0xff; out[1] = (v >> 8) & 0xff; out[2] = (v >> 16) & 0xff; out[3] = 0; out += 4;
            }
        } break;
        case ASIOSTInt32LSB: {
            int *out = (int *)output;
            while (count--) { *out++ = (int)(*input * 2147483520.0f); input += step; }
        } break;

        case ASIOSTInt16MSB: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                union { short int i; unsigned char u[2]; } v;
                v.i = (short int)(*input * 32767.0f); input += step;
                out[0] = v.u[1]; out[1] = v.u[0]; out += 2;
            }
        } break;
        case ASIOSTInt32MSB16: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                unsigned int v = (unsigned int)(int)(*input * 2147483520.0f) >> 16; input += step;
                out[0] = out[1] = 0; out[2] = (v >> 8) & 0xff; out[3] = v & 0xff; out += 4;
            }
        } break;
        case ASIOSTInt24MSB: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                int v = (int)(*input * 8388607.0f); input += step;
                out[2] = v & 0xff; out[1] = (v >> 8) & 0xff; out[0] = (v >> 16) & 0xff; out += 3;
            }
        } break;
        case ASIOSTInt32MSB18: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                unsigned int v = (unsigned int)(int)(*input * 2147483520.0f) >> 14; input += step;
                out[0] = 0; out[1] = (v >> 16) & 0xff; out[2] = (v >> 8) & 0xff; out[3] = v & 0xff; out += 4;
            }
        } break;
        case ASIOSTInt32MSB20: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                unsigned int v = (unsigned int)(int)(*input * 2147483520.0f) >> 12; input += step;
                out[0] = 0; out[1] = (v >> 16) & 0xff; out[2] = (v >> 8) & 0xff; out[3] = v & 0xff; out += 4;
            }
        } break;
        case ASIOSTInt32MSB24: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                unsigned int v = (unsigned int)(int)(*input * 2147483520.0f) >> 8; input += step;
                out[0] = 0; out[1] = (v >> 16) & 0xff; out[2] = (v >> 8) & 0xff; out[3] = v & 0xff; out += 4;
            }
        } break;
        case ASIOSTInt32MSB: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                union { int i; unsigned char u[4]; } v;
                v.i = (int)(*input * 2147483520.0f); input += step;
                out[0] = v.u[3]; out[1] = v.u[2]; out[2] = v.u[1]; out[3] = v.u[0]; out += 4;
            }
        } break;

        case ASIOSTFloat32LSB: {
            float *out = (float *)output;
            while (count--) { *out++ = *input; input += step; }
        } break;
        case ASIOSTFloat64LSB: {
            double *out = (double *)output;
            while (count--) { *out++ = *input; input += step; }
        } break;

        case ASIOSTFloat32MSB: {
            unsigned char *out = (unsigned char *)output;
            while (count--) { 
                union { float f; unsigned char u[4]; } v;
                v.f = *input; input += step;
                out[0] = v.u[3]; out[1] = v.u[2]; out[2] = v.u[1]; out[3] = v.u[0]; out += 4;
            }
        } break;
        case ASIOSTFloat64MSB: {
            unsigned char *out = (unsigned char *)output;
            while (count--) {
                union { double d; unsigned char u[8]; } v;
                v.d = *input; input += step;
                out[7] = v.u[0]; out[6] = v.u[1]; out[5] = v.u[2]; out[4] = v.u[3];
                out[3] = v.u[4]; out[2] = v.u[5]; out[1] = v.u[6]; out[0] = v.u[7]; out += 8;
            }
        } break;
        default: return;
    }
}

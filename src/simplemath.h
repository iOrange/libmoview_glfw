#pragma once

static void MakeOrtho2DMat(float left, float right, float top, float bottom, float zNear, float zFar, float mat[16]) {
    mat[0]  =  2.0f / (right - left);
    mat[5]  =  2.0f / (top - bottom);
    mat[10] = -2.0f / (zFar - zNear);
    mat[12] = -(right + left) / (right - left);
    mat[13] = -(top + bottom) / (top - bottom);
    mat[14] = -(zFar + zNear) / (zFar - zNear);
    mat[15] =  1.0f;

    mat[1] = mat[2] = mat[3] = mat[4] = mat[6] = mat[7] = mat[8] = mat[9] = mat[11] = 0.0f;
}


static void __minus_v2(float * _out, const float * _a, const float * _b)
{
    _out[0] = _a[0] - _b[0];
    _out[1] = _a[1] - _b[1];
}
//////////////////////////////////////////////////////////////////////////
static void __mul_v2_f(float * _out, float _v)
{
    _out[0] *= _v;
    _out[1] *= _v;
}

static float __dot_v3(const float * _a, const float * _b)
{
    return _a[0] * _b[0] + _a[1] * _b[1] + _a[2] * _b[2];
}

static void CalcPointUV(float * _out, const float * _a, const float * _b, const float * _c, const float * _auv, const float * _buv, const float * _cuv, const float * _point) {
    float _dAB[2];
    __minus_v2(_dAB, _b, _a);

    float _dAC[2];
    __minus_v2(_dAC, _c, _a);

    float inv_v = 1.f / (_dAB[0] * _dAC[1] - _dAB[1] * _dAC[0]);
    __mul_v2_f(_dAB, inv_v);
    __mul_v2_f(_dAC, inv_v);

    float _dac[2];
    _dac[0] = _dAC[0] * _a[1] - _dAC[1] * _a[0];
    _dac[1] = _dAB[1] * _a[0] - _dAB[0] * _a[1];

    float _duvAB[2];
    __minus_v2(_duvAB, _buv, _auv);

    float _duvAC[2];
    __minus_v2(_duvAC, _cuv, _auv);

    float pv[3] = {1.f, _point[0], _point[1]};
    float av[3] = {_dac[0], _dAC[1], -_dAC[0]};
    float bv[3] = {_dac[1], -_dAB[1], _dAB[0]};

    float a = __dot_v3(av, pv);
    float b = __dot_v3(bv, pv);

    float abv[3] = {1.f, a, b};
    float uv[3] = {_auv[0], _duvAB[0], _duvAC[0]};
    float vv[3] = {_auv[1], _duvAB[1], _duvAC[1]};

    float u = __dot_v3(uv, abv);
    float v = __dot_v3(vv, abv);

    _out[0] = u;
    _out[1] = v;
}

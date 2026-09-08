module Math;

public static const float PI = 3.1415926538;

public vector<float, N> FromSrgb<let N: int>(vector<float, N> v0, float gamma)
{
    return pow(v0, vector<float, N>(gamma));
}

public vector<float, N> ToSrgb<let N: int>(vector<float, N> v0, float gamma)
{
    return pow(v0, vector<float, N>(1.0 / gamma));
}
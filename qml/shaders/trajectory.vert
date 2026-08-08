void MAIN()
{
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);

    // Pull the raster overlay slightly toward the camera in clip space. This
    // avoids coplanar clipping on roads at any orientation without changing
    // trajectory vertices, bounds, or apparent world-space position.
    POSITION.z -= 0.000005 * POSITION.w;
}

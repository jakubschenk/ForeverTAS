void MAIN()
{
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);

    // Pull the raster overlay toward the camera by a constant 0.002% of the
    // normalized depth range. Clip-space bias stays effective at long range
    // without lifting the tube in world space or making it visibly float.
    POSITION.z -= 0.00002 * POSITION.w;
}

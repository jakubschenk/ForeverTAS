VARYING vec2 markUv;
VARYING float markBirthTime;
VARYING float markSurfaceClass;
VARYING vec4 markColor;

void MAIN()
{
    markUv = UV0;
    markBirthTime = UV1.x;
    markSurfaceClass = UV1.y;
    markColor = COLOR;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);

    // The mesh already sits slightly above the contact surface. This tiny
    // clip-space bias keeps coplanar authored map triangles from flickering
    // without making the marks visibly float at long camera distances.
    POSITION.z -= 0.00002 * POSITION.w;
}

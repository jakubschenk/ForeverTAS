VARYING vec2 markUv;
VARYING float markBirthTime;
VARYING float markSurfaceClass;
VARYING vec4 markColor;

void MAIN()
{
    if (markBirthTime > revealTimeSeconds + 0.0005)
        discard;

    // An unbounded derivative made a sub-pixel ribbon's feather wider than
    // the entire tyre strip, multiplying its alpha down to zero. Clamp the
    // edge region so raster coverage handles extreme minification while the
    // mark keeps a stable, antialiased core.
    float lateralDerivative = fwidth(markUv.x);
    float edgeWidth = clamp(lateralDerivative * 1.25, 0.025, 0.22);
    float edge = smoothstep(0.0, edgeWidth, markUv.x)
            * smoothstep(0.0, edgeWidth, 1.0 - markUv.x);

    // Keep manufactured-surface marks crisp while terrain marks read as a
    // softer disturbed strip. Derivative-aware modulation avoids distant
    // tread patterns turning into shimmer.
    float treadDerivative = fwidth(markUv.y);
    float treadWave = 0.5 + 0.5 * cos(markUv.y * 6.2831853);
    float treadStrength = 1.0 - smoothstep(0.12, 0.55, treadDerivative);
    float tread = mix(1.0, 0.82 + 0.18 * treadWave,
                      treadStrength * (1.0 - 0.55 * markSurfaceClass));
    float minificationBoost = mix(
            1.0, 2.0, smoothstep(0.35, 1.5, lateralDerivative));
    float alpha = min(markColor.a * edge * tread * minificationBoost, 0.68);
    if (alpha < 0.002)
        discard;

    FRAGCOLOR = vec4(markColor.rgb, alpha);
}

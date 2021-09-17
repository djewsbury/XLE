// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING

#include "CloudsForm.h"
#include "FluidHelper.h"
#include "Fluid.h"
#include "FluidAdvection.h"
#include "../RenderCore/IThreadContext.h"
#include "../Math/Noise.h"
#include "../Utility/Meta/ClassAccessorsImpl.h"

namespace SceneEngine
{
    static float KelvinToCelsius(float kelvin) { return kelvin - 273.15f; }
    static float CelsiusToKelvin(float celius) { return celius + 273.15f; }

    static float PressureAtAltitude(float kilometers, float lapseRate)
    {
            //
            //  Returns a value in pascals.
            //  For a given altitude, we can calculate the standard
            //  atmospheric pressure.
            //
            // "lapseRate" should be the temperature lapse rate in Kelvin per
            // kilometer (usually a value between 4 and 9)
            //
            //  Actually the "wetness" of the air should affect the lapse
            //  rate... But we'll ignore that here, and assume all of the 
            //  air is dry for this calculation.
            //
            //  We could build in a humidity constant into the simulation.
            //  This should adjust the lapse rate. Also the T0 temperature
            //  values should be adjustable.
            //
            //  See here for more information:
            //      https://en.wikipedia.org/wiki/Atmospheric_pressure
            //
        const float g = 9.81f;          // (in m/second)
        const float p0 = 101325.f;      // (in pascals, 1 standard atmosphere)
        const float T0 = CelsiusToKelvin(15.0f);         // (in kelvin, standard sea level temperature)
        const float Rd = 287.058f;      // (in J . kg^-1 . K^-1. This is the "specific" gas constant for dry air. It is R / M, where R is the gas constant, and M is the ideal gas constant)

            //  We have a few choices for the lape rate here.
            //      we're could use a value close to the "dry adiabatic lapse rate" (around 9.8 kelvin/km)
            //      Or we could use a value around 6 or 7 -- this is the average lapse rate in the troposphere
            //  see https://en.wikipedia.org/wiki/Lapse_rate
            //  The minimum value for lapse rate in the troposhere should be around 4
            //      (see http://www.iac.ethz.ch/edu/courses/bachelor/vertiefung/atmospheric_physics/Slides_2012/buoyancy.pdf)

        // roughly: p0 * std::exp(kilometers * g / (1000.f * T0 * Rd));     (see wikipedia page)
        // see also the "hypsometric equation" equation, similar to above
        return p0 * std::pow(1.f - kilometers * lapseRate / T0, g / (lapseRate / 1000.f * Rd));
    }

    static float ExnerFunction(float pressure)
    {
            //
            //  The potential temperature is defined based on the atmosphere pressure.
            //  So, we can go backwards as well and get the temperature from the potential
            //  temperature (if we know the pressure)
            //
            //  Note that if "pressure" comes from PressureAtAltitude, we will be 
            //  multiplying by p0, and then dividing it away again.
            //
            //  The Exner function is ratio of the potential temperature and absolute temperature.
            //  So,
            //      temperature = ExnerFunction * potentialTemperature
            //      potentialTemperature = temperature / ExnerFunction
            //

        const float p0 = 101325.f;  // (in pascals, 1 standard atmosphere)
        const float Rd = 287.058f;  // in J . kg^-1 . K^-1. Gas constant for dry air
        const float cp = 1005.f;    // in J . kg^-1 . K^-1. heat capacity of dry air at constant pressure
        const float kappa = Rd/cp;
        return std::pow(pressure/p0, kappa);
    }
    
///////////////////////////////////////////////////////////////////////////////////////////////////

    class Troposphere
    {
    public:
        float   GetVaporMixingRatio(unsigned gridY) const;
        float   GetPotentialTemperature(unsigned gridY) const;
        float   AltitudeKm(unsigned gridY) const;
        float   ZScale() const;
        float   GetEquilibriumMixingRatio(float potentialTemp, unsigned gridY) const;
        float   GetPotentialTemperatureRelease(unsigned gridY) const;

        float   AltitudeMinKm() const       { return _altitudeMin; }
        float   AltitudeMaxKm() const       { return _altitudeMax; }
        float   AirTemperature() const      { return _airTemperature; }
        float   RelativeHumidity() const    { return _relativeHumidity; }
        float   LapseRate() const           { return _lapseRate; }

        Troposphere(
            UInt2 gridDims,
            float altitudeMinKm, float altitudeMaxKm,
            float airTemperature,
            float relativeHumidity, float lapseRate);
        Troposphere();
        ~Troposphere();
    private:
        UInt2   _gridDims;
        float   _altitudeMin, _altitudeMax;
        float   _relativeHumidity;
        float   _airTemperature;
        float   _lapseRate;
    };

    float Troposphere::GetVaporMixingRatio(unsigned gridY) const
    {
            // RH = vaporMixingRatio/saturationMixingRatio
        return _relativeHumidity * GetEquilibriumMixingRatio(GetPotentialTemperature(gridY), gridY);
    }

    float Troposphere::GetPotentialTemperature(unsigned gridY) const
    {
        const auto altitudeKm = AltitudeKm(gridY);
        auto pressure = PressureAtAltitude(altitudeKm, _lapseRate);     // (precalculate these pressures)
        auto exner = ExnerFunction(pressure);
        return (_airTemperature - _lapseRate * altitudeKm) / exner;
    }

    float Troposphere::GetEquilibriumMixingRatio(float potentialTemp, unsigned gridY) const
    {
        auto altitudeKm = AltitudeKm(gridY);
        auto pressure = PressureAtAltitude(altitudeKm, _lapseRate);     // (precalculate these pressures)
        auto exner = ExnerFunction(pressure);
        auto T = potentialTemp * exner;

            // We can calculate the partial pressure for water vapor at saturation point.
            // Note that this pressure value is much smaller than the pressure value
            // calculated from PressureAtAltitude because it is a "partial" pressure.
            // It is the pressure for the vapor part only.
            //
            // (Here the pressure is calculated assuming all air is dry air, 
            // but it's not clear if that has a big impact). At this saturation
            // point, condensation can start to occur. And condensation is what we're
            // interested in!
            //
            // It would be useful if we could find a simplier relationship between
            // this value and potentialTemp -- by precalculating any terms that are
            // only dependent on the pressure (which is constant with altitude).
            //
            // See https://en.wikipedia.org/wiki/Clausius%E2%80%93Clapeyron_relation
            // This is called the August-Roche-Magnus formula.
            //
            // Note that this is very slightly different from the Harris' paper.
            // I'm using the equation from wikipedia, which gives a result in hectopascals.
            //
            // See also http://www.vaisala.com/Vaisala%20Documents/Application%20notes/Humidity_Conversion_Formulas_B210973EN-F.pdf
            // ("HUMIDITY CONVERSION FORMULAS") for an alternative formula.
            //
            //      (in pascals)
        // auto saturationPressure = 100.f * 6.1094f * XlExp((17.625f * T) / (T + 243.04f));
            // using (x+b)/(x+b+c) == 1 - c/(x+b+c)
            //  (17.625f * T) / (T + 243.04f) == 17.625f - 4283.58f / (T - 30.11)
            // using c * exp(e) = exp(e + ln(c)),  for positive c
        auto saturationPressure = XlExp(24.04f - 4283.58f / (T - 30.11f));
        // auto saturationPressure = 27570129378.f / XlExp(4283.58f / (T - 30.11f));
        // auto saturationPressure = 610.94f * std::pow(XlExp(1.f - 243.04f / (T - 30.11f)), 17.625f);

        #if defined(_DEBUG)
            auto Tc = KelvinToCelsius(potentialTemp * exner);
            auto oldSaturationPressure = 100.f * 6.1094f * XlExp((17.625f * Tc) / (Tc + 243.04f));
            assert(Equivalent(saturationPressure, oldSaturationPressure, oldSaturationPressure/1000.f));   // floating point inaccuracies put it a bit off
        #endif

            // We can use this to calculate the equilibrium point for the vapour mixing
            // ratio. 
            // However -- this might be a little inaccurate because it relies on our
            // calculation of the pressure from PressureAtAltitude. And that doesn't
            // take into account the humidity (which might be changing as a result of
            // the condensating occuring?)
                    
            //      The following is derived from
            //      ws  = es / (p - es) . Rd/Rv
            //          = (-p / (es-p) - 1) . Rd/Rv
            //          = c.(p / (p-es) - 1), where c = Rd/Rv ~= 0.622
            //      (see http://www.geog.ucsb.edu/~joel/g266_s10/lecture_notes/chapt03/oh10_3_01/oh10_3_01.html)
            //      It seems that a common approximation is just to ignore the es in
            //      the denominator of the first equation (since it should be small compared to total
            //      pressure). But that seems like a minor optimisation? Why not use the full equation?

        const auto Rd = 287.058f;  // in J . kg^-1 . K^-1. Specific gas constant for dry air
        const auto Rv = 461.495f;  // in J . kg^-1 . K^-1. Specific gas constant for water vapor
        const auto gasConstantRatio = Rd/Rv;   // ~0.622f;
        // return gasConstantRatio * (pressure / (pressure-saturationPressure) - 1.f);
        return gasConstantRatio * saturationPressure/(pressure-saturationPressure);
    }

    float Troposphere::GetPotentialTemperatureRelease(unsigned gridY) const
    {
            // Returns the potential temperature released during condensation
            // This is the temperature change when water vapor condenses.
            // We know the latent "heat" that is released -- but we can convert
            // that into temperature by using the heat capacity of dry air.
        auto pressure = PressureAtAltitude(AltitudeKm(gridY), _lapseRate);
        auto exner = ExnerFunction(pressure);

            // Note --  the "latentHeatOfVaporization" value in Harris' paper
            //          maybe incorrect. It looks like it was using the wrong units
            //          (kilojoules instead of joules)

            // We need to calculate the exner function for this calculation, as well
            // But the exner function is constant for each altitude -- so we could
            // just precalculate it for every gridY
        const auto latentHeatOfVaporization = 2260.f * 1000.f;  // in J . kg^-1 for water at 0 degrees celsius
        const auto cp = 1005.f;                                 // in J . kg^-1 . K^-1. heat capacity of dry air at constant pressure
        return (latentHeatOfVaporization / cp) / exner;
    }

    float Troposphere::AltitudeKm(unsigned gridY) const
    {
        return LinearInterpolate(
            _altitudeMin, _altitudeMax, float(gridY)/float(_gridDims[1]));
    }

    float Troposphere::ZScale() const
    {
        return (_altitudeMax-_altitudeMin) / float(_gridDims[1]);
    }

    Troposphere::Troposphere(
        UInt2 gridDims,
        float altitudeMin, float altitudeMax,
        float airTemperature, float relativeHumidity, float lapseRate)
    {
        _gridDims = gridDims;
        _altitudeMin = altitudeMin;
        _altitudeMax = altitudeMax;
        _airTemperature = airTemperature;
        _relativeHumidity = relativeHumidity;
        _lapseRate = lapseRate;
    }

    Troposphere::Troposphere() {}
    Troposphere::~Troposphere() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class CloudsForm2D::Pimpl
    {
    public:
        VectorX _velU[3];
        VectorX _velV[3];

        VectorX _vaporMixingRatio[2];      // qv
        VectorX _condensedMixingRatio[2];   // qc
        VectorX _potentialTemperature[2];   // theta

        UInt2 _dimsWithoutBorder;
        UInt2 _dimsWithBorder;
        unsigned _N;
        float _time;
        Float2 _mouseHover;

        PoissonSolver _poissonSolver;

        DiffusionHelper _velocityDiffusion;
        DiffusionHelper _vaporDiffusion;
        DiffusionHelper _condensedDiffusion;
        DiffusionHelper _temperatureDiffusion;
        EnforceIncompressibilityHelper _incompressibility;

        Troposphere _troposphere;
    };

    void CloudsForm2D::Tick(float deltaTime, const Settings& settings)
    {
        float dt = deltaTime;
        const auto N = _pimpl->_N;
        const auto dims = _pimpl->_dimsWithBorder;

        auto& velUT0 = _pimpl->_velU[0];
        auto& velUT1 = _pimpl->_velU[1];
        auto& velUSrc = _pimpl->_velU[2];
        auto& velUWorking = _pimpl->_velU[2];

        auto& velVT0 = _pimpl->_velV[0];
        auto& velVT1 = _pimpl->_velV[1];
        auto& velVSrc = _pimpl->_velV[2];
        auto& velVWorking = _pimpl->_velV[2];

        auto& potTempT1 = _pimpl->_potentialTemperature[1];
        auto& potTempWorking = _pimpl->_potentialTemperature[0];
        auto& qvT1 = _pimpl->_vaporMixingRatio[1];
        auto& qvSrc = _pimpl->_vaporMixingRatio[0];
        auto& qvWorking = _pimpl->_vaporMixingRatio[0];
        auto& qcT1 = _pimpl->_condensedMixingRatio[1];
        auto& qcWorking = _pimpl->_condensedMixingRatio[0];

            //
            // We will simulate condensation and buoyancy of
            // water vapour in the atmosphere.
            //
            // Here are our basic rules:
            //  >>  When vapor content reaches a certain equilibrium
            //      point, it starts to condense into clouds
            //  >>  The equilibrium point is a function of temperature
            //      and altitude (lower for higher altitudes and lower
            //      temperatures)
            //  >>  We add an upward force for temperatures higher than
            //      ambient, and a downward gravititational force for
            //      condensed water content
            //  >>  When vapor condenses, temperature increases and
            //      when condensation evaporates, temperature decreases
            //
            // There are some constants that affect the equilibrium point
            // and amount of buoyancy:
            //  >>  ambient temperature
            //  >>  humidity
            //  >>  temperature "lapse rate" (which is a measure of how fast
            //      temperature decreases with altitude)
            //
            // We're following the basic principles in 
            // Mark Jason Harris' PhD dissertation on cloud formation.
            //
            // We use a lot of "mixing ratios" here. The mixing ratio
            // is the ratio of a component in a mixture, relative to all
            // other components. see:
            //  https://en.wikipedia.org/wiki/Mixing_ratio
            //
            // So, if a single component makes up the entirity of a mixture,
            // then the mixing ratio convergences on infinity.
            //

            // (first, we need to recreate the Troposphere if the parameters have changed)
        if (    _pimpl->_troposphere.AltitudeMinKm() != settings._altitudeMin 
            ||  _pimpl->_troposphere.AltitudeMaxKm() != settings._altitudeMax
            ||  _pimpl->_troposphere.AirTemperature() != settings._airTemperature 
            ||  _pimpl->_troposphere.RelativeHumidity() != settings._relativeHumidity
            ||  _pimpl->_troposphere.LapseRate() != settings._lapseRate) {

            _pimpl->_troposphere = Troposphere(
                _pimpl->_dimsWithoutBorder, 
                settings._altitudeMin, settings._altitudeMax,
                settings._airTemperature, settings._relativeHumidity, settings._lapseRate);
        }

            // Vorticity confinement (additional) force
        VorticityConfinement(
            VectorField2D(&velUSrc, &velVSrc, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),           // last frame results
            settings._vorticityConfinement, deltaTime);

            // The strength of buoyancy is proportional to the reciprocal of
            // the referenceVirtualPotentialTemperature. So we can just the 
            // amount of bouyancy by changing this number.
        const auto zScale = _pimpl->_troposphere.ZScale();
        const auto g = 9.81f / 1000.f;  // (in km/second)

            // Buoyancy force
        const UInt2 border(0,1);
        for (unsigned y=border[1]; y<dims[1]-border[1]; ++y)
            for (unsigned x=border[0]; x<dims[0]-border[0]; ++x) {
                
                const auto i = y*dims[0]+x;
                auto potentialTemp = potTempT1[i];
                auto vapourMixingRatio = qvT1[i];
                auto condensationMixingRatio = qcT1[i];
                    
                    //
                    // We must calculate a buoyancy force for the parcel of air. We're going
                    // to use an equation that calculates the buoyancy from the temperature
                    // (relative to the ambient temperature). Our basic equation for buoyancy
                    // will be:
                    //      B = g * (T - T0) / T0
                    //      where T is the temperature, and T0 is the ambient temperature
                    //
                    // This is slightly different from what Harris uses in his paper:
                    //      B = g * T / T0
                    //
                    // Since temperature and potential temperature are proportional to each
                    // other, they can be used interchangeable (in other words, the "Exner" function
                    // factors out of the equation).
                    //
                    // T0 should be the temperature of the air that the parcel is "submerged" in.
                    // We will use the starting ambient temperature for T0.
                    //
                    // see:
                    //  http://www.iac.ethz.ch/edu/courses/bachelor/vertiefung/atmospheric_physics/Slides_2012/buoyancy.pdf
                    //  http://storm.colorado.edu/~dcn/ATOC5050/lectures/06_ThermoStabilty.pdf
                    //
                    // The "g" value is calibrated for dry air. But we can use the "virtual temperature"
                    // to get a result for moist air.
                    //
                    // In atmosphere thermodynamics, the "virtual temperature" of a parcel of air
                    // is a concept that allows us to simplify some equations. Given a "moist" packet
                    // of air -- that is, a packet with some water vapor -- it should behave the same
                    // as a dry packet of air at some temperature.
                    // That is what the virtual temperature is -- the temperature of a dry packet of air
                    // that would behave the same the given moist packet.
                    //
                    // It seems that the virtual temperature, for realistic vapour mixing ratios,
                    // is close to linear against the vapour mixing ratio. So we can
                    // use a simple equation to find it.
                    //
                    //  see also -- https://en.wikipedia.org/wiki/Virtual_temperature
                    //
                const auto ambientPotTemp = _pimpl->_troposphere.GetPotentialTemperature(y);

                    // Our basic equation:
                    //  B0 = (VT - T0) / T0
                    //
                    // We can simplify this a little bit to:
                    //  VT = T * (1 + 0.61 * qv)
                    //  B0 = (VT - T0) / T0
                    //     = VT / T0 - 1
                    //     = T/T0 - 1 + 0.61 * T / T0 * qv
                    //     = T/T0 * (1 + 0.61 * qv)  - 1
                auto B0 = (potentialTemp/ambientPotTemp) * (1.f + 0.61f * vapourMixingRatio) - 1.f;
                auto B = g * (B0 * settings._buoyancyAlpha - condensationMixingRatio * settings._buoyancyBeta);
                velVSrc[i] += B / zScale;

            }

        static float tempDissipate = 0.9985f;
        static float vaporDissipate = 0.9985f;  // we're adding so much extra vapor into the system that we need to remove it sometimes too
        // static float velDissipate = 0.985f;
        static float condensationDissipate = 0.9985f;
        static float edgeDissipate = 0.f;

        const float zr = 100.f;
        const float alpha = 1.f/7.f;

        static float gain = 0.5f;
        static float lacunarity = 2.1042f;
        static unsigned octaves = 4;

        for (unsigned c=0; c<N; ++c) {
            velUT0[c] = velUT1[c];
            velVT0[c] = velVT1[c];
            velUWorking[c] = velUT1[c] + dt * velUSrc[c];
            velVWorking[c] = velVT1[c] + dt * velVSrc[c];

            qcWorking[c] = qcT1[c];
            qvWorking[c] = qvT1[c] + dt * qvSrc[c];
            potTempWorking[c] = potTempT1[c];

                // In theory, the diffusion is the only kind of dissipation we should have for these
                // properties. But when we're wrapping around the edges, we will probably need some
                // extra artifical dissipation to a cycling that just gets stronger and stronger.
            const auto gridY = c/_pimpl->_dimsWithBorder[0];
            const auto ambientPotTemp = _pimpl->_troposphere.GetPotentialTemperature(gridY);
            const auto ambientVapor = _pimpl->_troposphere.GetVaporMixingRatio(gridY);
            potTempWorking[c] = LinearInterpolate(ambientPotTemp, potTempWorking[c], tempDissipate);
            // velUWorking[c] *= velDissipate;
            // velVWorking[c] *= velDissipate;
                // when the vapor/condensation dissipates, what happens to it's latent heat? Let's just ignore that...
            qvWorking[c] = LinearInterpolate(ambientVapor, qvWorking[c], vaporDissipate);
            qcWorking[c] = LinearInterpolate(0.f, qcWorking[c], condensationDissipate);

                // Adjust velocity based on ambient wind
                // Rather than adding wind in any single part, we'll just blend the 
                // simulated velocity value with the ambient wind value
                //
                // We will calculate the wind strength at altitude using the "power
                // wind profile":
                //      https://en.wikipedia.org/wiki/Wind_profile_power_law
                //
                // See also the "log wind profile" (which can be used to estimate
                // the wind strength in the bottom 100 meters, and takes into account
                // roughness of the terrain (such as forests and hills)

            auto altitudeKm = _pimpl->_troposphere.AltitudeKm(gridY);
            auto windStrength = settings._crossWindSpeed * std::pow(altitudeKm * 1000.f / zr, alpha);
            auto noiseValue = SimplexFBM(
                Float2(float(gridY) / 60.f, _pimpl->_time / 5.f),
                1.f, gain, lacunarity, octaves);
            windStrength *= (0.5f + 0.5f * noiseValue) / 1000.f / zScale;      // assuming square grid -- using z scale for xy value
            const auto windFactor = 0.075f;
            velUWorking[c] = LinearInterpolate(velUWorking[c], windStrength, windFactor);

                // add a little random up/down movement as well
            // velVWorking[c] += windStrength * 0.1f * SimplexFBM(
            //     Float2(float(gridY) / 78.f, _pimpl->_time / 7.8f),
            //     1.f, gain, lacunarity, octaves);
        }

        // const auto marginFlags = 0u;
        const auto wrapEdges = 1u<<0u;
        _pimpl->_velocityDiffusion.Execute(
            _pimpl->_poissonSolver,
            VectorField2D(&velUWorking, &velVWorking, _pimpl->_dimsWithBorder),
            settings._viscosity, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, wrapEdges, "Velocity");

        _pimpl->_condensedDiffusion.Execute(
            _pimpl->_poissonSolver,
            ScalarField2D(&qcWorking, _pimpl->_dimsWithBorder),
            settings._condensedDiffusionRate, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, wrapEdges, "Condensed");

        _pimpl->_vaporDiffusion.Execute(
            _pimpl->_poissonSolver,
            ScalarField2D(&qvWorking, _pimpl->_dimsWithBorder),
            settings._vaporDiffusionRate, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, wrapEdges, "Vapor");

        _pimpl->_temperatureDiffusion.Execute(
            _pimpl->_poissonSolver,
            ScalarField2D(&potTempWorking, _pimpl->_dimsWithBorder),
            settings._temperatureDiffusionRate, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, wrapEdges, "Temperature");

            // Fill in the bottom row with vapor and temperature entering
            // from landscape below
            // up is +Y, so bottom most row is row 0
        static Float2 v_scale = Float2(485.5f, 3.6f);
        static Float2 t_scale = Float2(210.6f, 2.4f);
        static Float2 u_scale = Float2(234.3f, 15.7f);

            // for reference 20g/kg is a very high value for qv, as you might find in the tropics (see Atmospheric Science: An Introductory Survey, pg 80)
        const auto v_amp = settings._inputVapor;
        const auto u_amp = settings._inputUpdraft;
        const auto t_amp = settings._inputTemperature;
        for (unsigned x=0; x<_pimpl->_dimsWithBorder[0]; ++x) {
            auto vaporNoiseValue = SimplexFBM(
                Float2(float(x) / v_scale[0], _pimpl->_time / v_scale[1]),
                1.f, gain, lacunarity, octaves);
            auto tempNoiseValue = SimplexFBM(
                Float2(float(x) / t_scale[0], _pimpl->_time / t_scale[1]),
                1.f, gain, lacunarity, octaves);
            auto updraftNoiseValue = SimplexFBM(
                Float2(float(x) / u_scale[0], _pimpl->_time / u_scale[1]),
                1.f, gain, lacunarity, octaves);

            qvWorking[x]  = _pimpl->_troposphere.GetVaporMixingRatio(0);
            vaporNoiseValue -= 0.25f;
            qvWorking[x] += std::max(0.f, vaporNoiseValue * vaporNoiseValue * vaporNoiseValue) * v_amp;
            potTempWorking[x]  = _pimpl->_troposphere.GetPotentialTemperature(0);
            potTempWorking[x] += tempNoiseValue * tempNoiseValue * tempNoiseValue * t_amp;
            velUWorking[x]  = 0.f;
            velVWorking[x] += std::max(0.f, std::pow(updraftNoiseValue, 5.f)) * u_amp;
            qcWorking[x]    = 0.f;

                // shouldn't really need to set the "T1" values here
            qvT1[x]   = qvWorking[x];
            potTempT1[x] = potTempWorking[x];
            velUT1[x] = velUWorking[x];
            velVT1[x] = velVWorking[x];
            qcT1[x]   = qcWorking[x];

                // We can set the top row to ambient values -- but that creates
                // massive instability along the top of the simulation.
                // It effectively locks the dynamic part of the simulation into the simulated 
                // region -- the turbulence gets trapped inside
                // Perhaps it is best just to fade to the ambient values -- in that
                // way simulating a bit of diffusion and advection upwards.
                // Ideally the simulation should be fairly stable in the upper part, before
                // it gets to the edge
            const auto i2 = (_pimpl->_dimsWithBorder[1]-1)*_pimpl->_dimsWithBorder[0]+x;
            qcT1[i2] = qcWorking[i2] = 0.f; // LinearInterpolate(  qcWorking[i2], 0.f, 0.05f);
            qvT1[i2] = qvWorking[i2] = 
                // _pimpl->_troposphere.GetVaporMixingRatio(_pimpl->_dimsWithBorder[1]-1);
                std::min(qvWorking[i2], LinearInterpolate(_pimpl->_troposphere.GetVaporMixingRatio(_pimpl->_dimsWithBorder[1]-1), qvWorking[i2], edgeDissipate));
            potTempT1[i2] = potTempWorking[i2] = 
                // _pimpl->_troposphere.GetPotentialTemperature(_pimpl->_dimsWithBorder[1]-1);
                std::min(potTempWorking[i2], LinearInterpolate(_pimpl->_troposphere.GetPotentialTemperature(_pimpl->_dimsWithBorder[1]-1), potTempWorking[i2], edgeDissipate));
        }

        if (settings._obstructionType != 0) {
                // Simple obstruction simulation in the middle!
                // This isn't very accurate because the advection and
                // diffusion steps won't react properly with it. But
                // it can add some interesting variation to the simulation.
            const Float2 obsCenter = dims/2 + Float2(0.5f, 0.5f);; // Float2(dims[0]/2.f+.5f, dims[1]-.5f); // 
            const float obsRadius = 8.f; // dims[0]/2.f; // 
            for (unsigned y=border[1]; y<dims[1]-border[1]; ++y)
                for (unsigned x=border[0]; x<dims[0]-border[0]; ++x) {
                    auto offset = Float2(float(x) - obsCenter[0], float(y) - obsCenter[1]);
                    auto mag = Magnitude(offset);
                    if (mag < obsRadius) {
                        const auto i = (y*_pimpl->_dimsWithBorder[0])+x;
                        qcWorking[i] = 0.f;
                        qvWorking[i] = _pimpl->_troposphere.GetVaporMixingRatio(y);
                        potTempWorking[i] = _pimpl->_troposphere.GetPotentialTemperature(y) - 10.f;
                        velUWorking[i] = 0.2f * float(offset[0])/mag;
                        velVWorking[i] = 0.2f * float(offset[1])/mag;
                    }
                }
        }
        
        AdvectionSettings advSettings {
            (AdvectionMethod)settings._advectionMethod, 
            (AdvectionInterp)settings._interpolationMethod, settings._advectionSteps,
            AdvectionBorder::Wrap, AdvectionBorder::None, AdvectionBorder::Margin
        };
        PerformAdvection(
            VectorField2D(&velUT1,      &velVT1,        _pimpl->_dimsWithBorder),
            VectorField2D(&velUWorking, &velVWorking,   _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0,      &velVT0,        _pimpl->_dimsWithBorder),
            VectorField2D(&velUWorking, &velVWorking,   _pimpl->_dimsWithBorder),
            deltaTime, advSettings);
        
            // Apply reflection to "V" velocity along the top edge
        for (unsigned x=0; x<_pimpl->_dimsWithBorder[0]; ++x) {
            const auto i2 = (_pimpl->_dimsWithBorder[1]-1)*_pimpl->_dimsWithBorder[0]+x;
            velUT1[i2] = velUWorking[i2] = LinearInterpolate(0.f, velUWorking[i2], edgeDissipate);
            velVT1[i2] = velVWorking[i2] = LinearInterpolate(-velVWorking[i2-_pimpl->_dimsWithBorder[0]], velVWorking[i2], edgeDissipate);
        }

        _pimpl->_incompressibility.Execute(
            _pimpl->_poissonSolver,
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            (PoissonSolver::Method)settings._enforceIncompressibilityMethod,
            wrapEdges);

            // note -- advection of all 3 of these properties should be very
            // similar, since the velocity field doesn't change. Rather that 
            // performing the advection multiple times, we could just do it
            // once and reuse the result for each.
            // Actually, we could even use the same advection result we got
            // while advecting velocity -- it's unclear how that would change
            // the result (especially since that advection happens before we
            // enforce incompressibility).
        PerformAdvection(
            ScalarField2D(&qcT1, _pimpl->_dimsWithBorder),
            ScalarField2D(&qcWorking, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0, &velVT0, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            deltaTime, advSettings);
        PerformAdvection(
            ScalarField2D(&qvT1, _pimpl->_dimsWithBorder),
            ScalarField2D(&qvWorking, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0, &velVT0, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            deltaTime, advSettings);
        PerformAdvection(
            ScalarField2D(&potTempT1, _pimpl->_dimsWithBorder),
            ScalarField2D(&potTempWorking, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0, &velVT0, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            deltaTime, advSettings);

            // Perform condenstation after advection
            // Does it matter much if we do this before or after advection?
        for (unsigned y=border[1]; y<dims[1]-border[1]; ++y)
            for (unsigned x=border[0]; x<dims[0]-border[0]; ++x) {

                    //
                    // When the water vapour mixing ratio exceeds a certain point, condensation
                    // may occur. This point is called the equilibrium point. It depends on
                    // the temperature of the parcel. If any vapor condenses, we must release some
                    // latent heat into surrounding the atmosphere.
                    //
                    // So there are 2 controlling variables here:
                    //  * equilibrium mixing ratio -- the threshold at which vapor starts to condense
                    //  * potential temperature release -- how much potential temperature is released on condensation
                    //

                const auto i = y*dims[0]+x;
                auto& potentialTemp = potTempT1[i];
                auto& vapourMixingRatio = qvT1[i];
                auto& condensationMixingRatio = qcT1[i];

                auto equilibriumMixingRatio = _pimpl->_troposphere.GetEquilibriumMixingRatio(potentialTemp, y);
                auto potTempRelease = _pimpl->_troposphere.GetPotentialTemperatureRelease(y);

                    // Once we know our mixing ratio is above the equilibrium point -- how quickly should we
                    // get condensation? Delta time should be a factor here, but the integration isn't very
                    // accurate.
                    // Adjusting mixing ratios like this seems awkward. But I guess that the values tracked
                    // should generally be small relative to the total mixture (ie, ratios should be much
                    // smaller than 1.f). Otherwise changing one ratio effectively changes the meaning of
                    // the other (given that they are ratios against all other substances in the mixture).
                    // But, then again, in this simple model the condensationMixingRatio doesn't effect the 
                    // equilibriumMixingRatio equation. Only the change in temperature (which is adjusted 
                    // here) effects the equilibriumMixingRatio -- which can result in oscillation in some
                    // cases.

                float deltaCondensation = 0.f;
                if (vapourMixingRatio > equilibriumMixingRatio) {
                    auto upperDifference = vapourMixingRatio - equilibriumMixingRatio;
                    deltaCondensation = std::min(1.f, settings._condensationSpeed) * upperDifference;
                } else if (vapourMixingRatio < settings._evaporateThreshold * equilibriumMixingRatio) {
                    auto lowerDifference = vapourMixingRatio - settings._evaporateThreshold * equilibriumMixingRatio;
                    deltaCondensation = std::min(1.f, settings._condensationSpeed) * lowerDifference;
                    deltaCondensation = std::max(deltaCondensation, -condensationMixingRatio);
                }

                vapourMixingRatio -= deltaCondensation;
                condensationMixingRatio += deltaCondensation;

                    // Delta condensation should effect the temperature, as well
                    // When water vapour condenses, it releases its latent heat (this is heat
                    // that was absorbed when the vapor was originally evaporated).
                    //
                    // Note that the change in temperature will change the equilibrium
                    // mixing ratio (for the next update). And it will also effect buoyancy.
                    // So the heat change should have an important effect on the dynamics of
                    // the system.
                    //
                    // We have to be careful, because this can make conversion from vapor to
                    // condensed back to vapor unstable (which can cause rapid fluxuations back
                    // and forth). To avoid this, we adjust the conversion points and maybe
                    // leak some heat so that less heat is absorbed when cloud is evaporating
                    // back into vapor.
                                    
                auto deltaPotTemp = potTempRelease * deltaCondensation;
                potentialTemp += deltaPotTemp * settings._temperatureChangeSpeed;
            }

        for (unsigned c=0; c<N; ++c) {
            velUSrc[c] = 0.f;
            velVSrc[c] = 0.f;
            qvSrc[c] = 0.f;
        }

        _pimpl->_time += deltaTime;
    }

    void CloudsForm2D::OldTick(float deltaTime, const Settings& settings)
    {
        float dt = deltaTime;
        const auto N = _pimpl->_N;
        const auto dims = _pimpl->_dimsWithBorder;

        auto& velUT0 = _pimpl->_velU[0];
        auto& velUT1 = _pimpl->_velU[1];
        auto& velUSrc = _pimpl->_velU[2];
        auto& velUWorking = _pimpl->_velU[2];

        auto& velVT0 = _pimpl->_velV[0];
        auto& velVT1 = _pimpl->_velV[1];
        auto& velVSrc = _pimpl->_velV[2];
        auto& velVWorking = _pimpl->_velV[2];

        auto& potTempT1 = _pimpl->_potentialTemperature[1];
        auto& potTempWorking = _pimpl->_potentialTemperature[0];
        auto& qvT1 = _pimpl->_vaporMixingRatio[1];
        auto& qvSrc = _pimpl->_vaporMixingRatio[0];
        auto& qvWorking = _pimpl->_vaporMixingRatio[0];
        auto& qcT1 = _pimpl->_condensedMixingRatio[1];
        auto& qcWorking = _pimpl->_condensedMixingRatio[0];

            //
            // Following Mark Jason Harris' PhD dissertation,
            // we will simulate condensation and buoyancy of
            // water vapour in the atmosphere.  
            //
            // We use a lot of "mixing ratios" here. The mixing ratio
            // is the ratio of a component in a mixture, relative to all
            // other components. see:
            //  https://en.wikipedia.org/wiki/Mixing_ratio
            //
            // So, if a single component makes up the entirity of a mixture,
            // then the mixing ratio convergences on infinity.
            //

            // Vorticity confinement (additional) force
        VorticityConfinement(
            VectorField2D(&velUSrc, &velVSrc, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),           // last frame results
            settings._vorticityConfinement, deltaTime);

            // The strength of buoyancy is proportional to the reciprocal of
            // the referenceVirtualPotentialTemperature. So we can just the 
            // amount of bouyancy by changing this number.
        const auto g = 9.81f / 1000.f;  // (in km/second)
        const auto ambientTemperature = CelsiusToKelvin(15.f);

        const auto zScale = _pimpl->_troposphere.ZScale();

            // Buoyancy force
        const UInt2 border(1,1);
        for (unsigned y=border[1]; y<dims[1]-border[1]; ++y)
            for (unsigned x=border[0]; x<dims[0]-border[0]; ++x) {
                
                    //
                    // As described by Harris, we will ignore the effect of local
                    // pressure changes, and use his equation (2.10)
                    //
                const auto i = y*dims[0]+x;
                auto potentialTemp = potTempT1[i];
                auto vapourMixingRatio = qvT1[i];
                auto condensationMixingRatio = qcT1[i];

                    //
                    // In atmosphere thermodynamics, the "virtual temperature" of a parcel of air
                    // is a concept that allows us to simplify some equations. Given a "moist" packet
                    // of air -- that is, a packet with some water vapor -- it should behave the same
                    // as a dry packet of air at some temperature.
                    // That is what the virtual temperature is -- the temperature of a dry packet of air
                    // that would behave the same the given moist packet.
                    //
                    // It seems that the virtual temperature, for realistic vapour mixing ratios,
                    // is close to linear against the vapour mixing ratio. So we can
                    // use a simple equation to find it.
                    //
                    //  see also -- https://en.wikipedia.org/wiki/Virtual_temperature
                    //
                    // The "potential temperature" of a parcel is proportional to the "temperature"
                    // of that parcel.
                    // and, since
                    //  virtual temperature ~= T . (1 + 0.61qv)
                    // we can apply the same equation to the potential temperature.
                    //
                auto virtualPotentialTemp = 
                    potentialTemp * (1.f + 0.61f * vapourMixingRatio);

                auto altitudeKm = _pimpl->_troposphere.AltitudeKm(y);
                auto lapseRate = _pimpl->_troposphere.LapseRate();
                auto pressure = PressureAtAltitude(altitudeKm, lapseRate);     // (precalculate these pressures)
                auto exner = ExnerFunction(pressure);
                const auto referenceVirtualPotentialTemperature = 295.f / exner;

                    //
                    // As per Harris, we use the condenstation mixing ratio for the "hydrometeors" mixing
                    // ratio here. Note that this final equation is similar to our simple smoke buoyancy
                    //  --  the force up is linear against temperatures and density of particles 
                    //      in the air
                    // We need to scale the velocity according the scaling system of our grid. In the CFD
                    // system, coordinates are in grid units. 
                    // 
                // auto B = 
                //     g * (virtualPotentialTemp / referenceVirtualPotentialTemperature - condensationMixingRatio);
                // B /= zScale;
                // 
                //     // B is now our buoyant force per unit mass -- so it is just our acceleration.
                // velVSrc[i] -= B;
                (void)virtualPotentialTemp; (void)referenceVirtualPotentialTemperature; (void)condensationMixingRatio;

                const auto temp = potentialTemp * exner;
                auto virtualTemperature = temp * (1.f + 0.61f * vapourMixingRatio);
                auto temperatureBuoyancy = (virtualTemperature - ambientTemperature) / ambientTemperature;
                auto B = g * (temperatureBuoyancy * settings._buoyancyAlpha - condensationMixingRatio * settings._buoyancyBeta);
                velVSrc[i] += B / zScale;
            }

        for (unsigned c=0; c<N; ++c) {
            velUT0[c] = velUT1[c];
            velVT0[c] = velVT1[c];
            velUWorking[c] = velUT1[c] + dt * velUSrc[c];
            velVWorking[c] = velVT1[c] + dt * velVSrc[c];

            qcWorking[c] = qcT1[c]; //  + dt * qcSrc[c];
            qvWorking[c] = qvT1[c] + dt * qvSrc[c];
            potTempWorking[c] = potTempT1[c]; // + dt * temperatureSrc[c];
        }

        _pimpl->_velocityDiffusion.Execute(
            _pimpl->_poissonSolver,
            VectorField2D(&velUWorking, &velVWorking, _pimpl->_dimsWithBorder),
            settings._viscosity, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, 0u, "Velocity");

        AdvectionSettings advSettings {
            (AdvectionMethod)settings._advectionMethod, 
            (AdvectionInterp)settings._interpolationMethod, settings._advectionSteps,
            AdvectionBorder::Margin, AdvectionBorder::Margin, AdvectionBorder::Margin
        };
        PerformAdvection(
            VectorField2D(&velUT1,      &velVT1,        _pimpl->_dimsWithBorder),
            VectorField2D(&velUWorking, &velVWorking,   _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0,      &velVT0,        _pimpl->_dimsWithBorder),
            VectorField2D(&velUWorking, &velVWorking,   _pimpl->_dimsWithBorder),
            deltaTime, advSettings);
        
        ReflectUBorder2D(velUT1, _pimpl->_dimsWithBorder, ~0u);
        ReflectVBorder2D(velVT1, _pimpl->_dimsWithBorder, ~0u);
        _pimpl->_incompressibility.Execute(
            _pimpl->_poissonSolver,
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            (PoissonSolver::Method)settings._enforceIncompressibilityMethod,
            0u);

            // note -- advection of all 3 of these properties should be very
            // similar, since the velocity field doesn't change. Rather that 
            // performing the advection multiple times, we could just do it
            // once and reuse the result for each.
            // Actually, we could even use the same advection result we got
            // while advecting velocity -- it's unclear how that would change
            // the result (especially since that advection happens before we
            // enforce incompressibility).
        _pimpl->_condensedDiffusion.Execute(
            _pimpl->_poissonSolver,
            ScalarField2D(&qcWorking, _pimpl->_dimsWithBorder),
            settings._condensedDiffusionRate, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, 0u, "Condensed");
        PerformAdvection(
            ScalarField2D(&qcT1, _pimpl->_dimsWithBorder),
            ScalarField2D(&qcWorking, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0, &velVT0, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            deltaTime, advSettings);

        _pimpl->_vaporDiffusion.Execute(
            _pimpl->_poissonSolver,
            ScalarField2D(&qvWorking, _pimpl->_dimsWithBorder),
            settings._vaporDiffusionRate, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, 0u, "Vapor");
        PerformAdvection(
            ScalarField2D(&qvT1, _pimpl->_dimsWithBorder),
            ScalarField2D(&qvWorking, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0, &velVT0, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            deltaTime, advSettings);

        _pimpl->_temperatureDiffusion.Execute(
            _pimpl->_poissonSolver,
            ScalarField2D(&potTempWorking, _pimpl->_dimsWithBorder),
            settings._temperatureDiffusionRate, deltaTime, (PoissonSolver::Method)settings._diffusionMethod, 0u, "Temperature");
        PerformAdvection(
            ScalarField2D(&potTempT1, _pimpl->_dimsWithBorder),
            ScalarField2D(&potTempWorking, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT0, &velVT0, _pimpl->_dimsWithBorder),
            VectorField2D(&velUT1, &velVT1, _pimpl->_dimsWithBorder),
            deltaTime, advSettings);

            // Perform condenstation after advection
            // Does it matter much if we do this before or after advection?
        for (unsigned y=border[1]; y<dims[1]-border[1]; ++y)
            for (unsigned x=border[0]; x<dims[0]-border[0]; ++x) {

                    // When the water vapour mixing ratio exceeds a certain point, condensation
                    // may occur. This point is called the saturation point. It depends on
                    // the temperature of the parcel.

                const auto i = y*dims[0]+x;
                auto& potentialTemp = potTempT1[i];
                auto& vapourMixingRatio = qvT1[i];
                auto& condensationMixingRatio = qcT1[i];

                    // There are a lot of constants here... Maybe there is a better
                    // way to express this equation?

                auto altitudeKm = _pimpl->_troposphere.AltitudeKm(y);
                auto lapseRate = _pimpl->_troposphere.LapseRate();
                auto pressure = PressureAtAltitude(altitudeKm, lapseRate);     // (precalculate these pressures)
                auto exner = ExnerFunction(pressure);
                auto T = KelvinToCelsius(potentialTemp * exner);

                    // We can calculate the partial pressure for water vapor at saturation point.
                    // Note that this pressure value is much smaller than the pressure value
                    // calculated from PressureAtAltitude because it is a "partial" pressure.
                    // It is the pressure for the vapor part only.
                    //
                    // (Here the pressure is calculated assuming all air is dry air, 
                    // but it's not clear if that has a big impact). At this saturation
                    // point, condensation can start to occur. And condensation is what we're
                    // interested in!
                    //
                    // It would be useful if we could find a simplier relationship between
                    // this value and potentialTemp -- by precalculating any terms that are
                    // only dependent on the pressure (which is constant with altitude).
                    //
                    // See https://en.wikipedia.org/wiki/Clausius%E2%80%93Clapeyron_relation
                    // This is called the August-Roche-Magnus formula.
                    //
                    // Note that this is very slightly different from the Harris' paper.
                    // I'm using the equation from wikipedia, which gives a result in hectopascals.
                    //
                    // See also http://www.vaisala.com/Vaisala%20Documents/Application%20notes/Humidity_Conversion_Formulas_B210973EN-F.pdf
                    // ("HUMIDITY CONVERSION FORMULAS") for an alternative formula.
                    //
                    //      (in pascals)
                auto saturationPressure = 100.f * 6.1094f * XlExp((17.625f * T) / (T + 243.04f));

                    // We can use this to calculate the equilibrium point for the vapour mixing
                    // ratio. 
                    // However -- this might be a little inaccurate because it relies on our
                    // calculation of the pressure from PressureAtAltitude. And that doesn't
                    // take into account the humidity (which might be changing as a result of
                    // the condensating occuring?)
                    
                    //      The following is derived from
                    //      ws  = es / (p - es) . Rd/Rv
                    //          = (-p / (es-p) - 1) . Rd/Rv
                    //          = c.(p / (p-es) - 1), where c = Rd/Rv ~= 0.622
                    //      (see http://www.geog.ucsb.edu/~joel/g266_s10/lecture_notes/chapt03/oh10_3_01/oh10_3_01.html)
                    //      It seems that a common approximation is just to ignore the es in
                    //      the denominator of the first equation (since it should be small compared to total
                    //      pressure). But that seems like a minor optimisation? Why not use the full equation?

                const auto Rd = 287.058f;  // in J . kg^-1 . K^-1. Gas constant for dry air
                const auto Rv = 461.495f;  // in J . kg^-1 . K^-1. Gas constant for water vapor
                const auto gasConstantRatio = Rd/Rv;   // ~0.622f;
                float equilibriumMixingRatio = 
                    gasConstantRatio * (pressure / (pressure-saturationPressure) - 1.f);

                    // Once we know our mixing ratio is above the equilibrium point -- how quickly should we
                    // get condensation? Delta time should be a factor here, but the integration isn't very
                    // accurate.
                    // Adjusting mixing ratios like this seems awkward. But I guess that the values tracked
                    // should generally be small relative to the total mixture (ie, ratios should be much
                    // smaller than 1.f). Otherwise changing one ratio effectively changes the meaning of
                    // the other (given that they are ratios against all other substances in the mixture).
                    // But, then again, in this simple model the condensationMixingRatio doesn't effect the 
                    // equilibriumMixingRatio equation. Only the change in temperature (which is adjusted 
                    // here) effects the equilibriumMixingRatio.

                auto difference = vapourMixingRatio - equilibriumMixingRatio;
                auto deltaCondensation = std::min(1.f, deltaTime * settings._condensationSpeed) * difference;
                deltaCondensation = std::max(deltaCondensation, 0.f); // -condensationMixingRatio);
                vapourMixingRatio -= deltaCondensation;
                condensationMixingRatio += deltaCondensation;

                    // Delta condensation should effect the temperature, as well
                    // When water vapour condenses, it releases its latent heat.
                    // Note that the change in temperature will change the equilibrium
                    // mixing ratio (for the next update).

                    // Note -- "latentHeatOfVaporization" value comes from Harris' paper.
                    //          But we should check this. It appears that the units may be
                    //          out in that paper. It maybe should be 2.5 x 10^6, or thereabouts?

                const auto latentHeatOfVaporization = 2260.f * 1000.f;  // in J . kg^-1 for water at 0 degrees celsius
                const auto cp = 1005.f;                                 // in J . kg^-1 . K^-1. heat capacity of dry air at constant pressure
                auto deltaPotTemp = latentHeatOfVaporization / (cp * exner) * deltaCondensation;
                potentialTemp += deltaPotTemp * settings._temperatureChangeSpeed;
            }

        for (unsigned c=0; c<N; ++c) {
            velUSrc[c] = 0.f;
            velVSrc[c] = 0.f;
            qvSrc[c] = 0.f;
        }
    }

    void CloudsForm2D::AddVapor(UInt2 coords, float amount)
    {
        if (coords[0] < _pimpl->_dimsWithoutBorder[0] && coords[1] < _pimpl->_dimsWithoutBorder[1]) {
            unsigned i = (coords[0]+1) + (coords[1]+1) * _pimpl->_dimsWithBorder[0];
            _pimpl->_vaporMixingRatio[0][i] += amount;
        }
    }

    void CloudsForm2D::AddVelocity(UInt2 coords, Float2 vel)
    {
        if (coords[0] < _pimpl->_dimsWithoutBorder[0] && coords[1] < _pimpl->_dimsWithoutBorder[1]) {
            unsigned i = (coords[0]+1) + (coords[1]+1) * _pimpl->_dimsWithBorder[0];
            _pimpl->_velU[2][i] += vel[0];
            _pimpl->_velV[2][i] += vel[1];
        }
    }

    void CloudsForm2D::RenderDebugging(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        FluidDebuggingMode debuggingMode)
    {
        static float qcMin = 0.f, qcMax = 1e-3f;
        static float qvMin = 0.f, qvMax = 1e-2f;
        static float tMin = CelsiusToKelvin(15.f), tMax = CelsiusToKelvin(32.f);
        switch (debuggingMode) {
        case FluidDebuggingMode::Density:
            RenderFluidDebugging2D(
                context, parserContext, RenderFluidMode::Scalar,
                _pimpl->_dimsWithBorder, qcMin, qcMax,
                { _pimpl->_condensedMixingRatio[1].data() });
            break;

        case FluidDebuggingMode::Velocity:
            RenderFluidDebugging2D(
                context, parserContext, RenderFluidMode::Vector,
                _pimpl->_dimsWithBorder, 0.f, 1.f,
                { _pimpl->_velU[1].data(), _pimpl->_velV[1].data() });
            break;

        case FluidDebuggingMode::Temperature:
            RenderFluidDebugging2D(
                context, parserContext, RenderFluidMode::Scalar,
                _pimpl->_dimsWithBorder, tMin, tMax,
                { _pimpl->_potentialTemperature[1].data() });
            break;

        case FluidDebuggingMode::Vapor:
            RenderFluidDebugging2D(
                context, parserContext, RenderFluidMode::Scalar,
                _pimpl->_dimsWithBorder, qvMin, qvMax,
                { _pimpl->_vaporMixingRatio[1].data() });
            break;

        case FluidDebuggingMode::Divergence:
            RenderFluidDebugging2D(
                context, parserContext, RenderFluidMode::Scalar,
                _pimpl->_dimsWithBorder, 0.f, 1.f,
                { _pimpl->_incompressibility.GetDivergence() });
            break;
        }
    }

    void CloudsForm2D::OnMouseMove(Float2 coords)
    {
        _pimpl->_mouseHover = coords;
    }

    UInt2 CloudsForm2D::GetDimensions() const { return _pimpl->_dimsWithBorder; }

    CloudsForm2D::CloudsForm2D(UInt2 dimensions)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_dimsWithoutBorder = dimensions;
        _pimpl->_dimsWithBorder = dimensions + UInt2(2, 2);
        auto N = _pimpl->_dimsWithBorder[0] * _pimpl->_dimsWithBorder[1];
        _pimpl->_N = N;
        _pimpl->_time = 0.f;
        _pimpl->_mouseHover = Float2(0.f, 0.f);

        for (unsigned c=0; c<dimof(_pimpl->_velU); ++c) {
            _pimpl->_velU[c] = VectorX(N);
            _pimpl->_velV[c] = VectorX(N);
            _pimpl->_velU[c].fill(0.f);
            _pimpl->_velV[c].fill(0.f);
        }

        Settings defaults;
        _pimpl->_troposphere = Troposphere(
            _pimpl->_dimsWithBorder, 
            defaults._altitudeMin, defaults._altitudeMax, 
            defaults._airTemperature, defaults._relativeHumidity, defaults._lapseRate);
        
        for (unsigned c=0; c<dimof(_pimpl->_vaporMixingRatio); ++c) {

            _pimpl->_potentialTemperature[c] = VectorX(N);
            _pimpl->_condensedMixingRatio[c] = VectorX(N);
            _pimpl->_vaporMixingRatio[c] = VectorX(N);

                // we should start with some vapor in the atmosphere. The
                // amount depends on the humidity of the atmosphere.
                // The "specific humidity" is the ratio of the vapor mass to 
                // total mass -- and this is approximate equal to the vapor
                // mixing ratio (because the ratio is very small).
                // So, we can use formulas for specific humidity to calculate a
                // starting point.

            const auto& dims = _pimpl->_dimsWithBorder;
            for (unsigned y=0; y<dims[1]; ++y)
                for (unsigned x=0; x<dims[0]; ++x) {
                    auto i = y*dims[0]+x;
                    _pimpl->_potentialTemperature[c][i] = _pimpl->_troposphere.GetPotentialTemperature(y);
                    _pimpl->_vaporMixingRatio[c][i] = _pimpl->_troposphere.GetVaporMixingRatio(y);

                        // Starting with zero condensation... But we could initialise
                        // with with a noise field; just to get started.
                    _pimpl->_condensedMixingRatio[c][i] = 0.f;
                }

        }

        _pimpl->_poissonSolver = PoissonSolver(2, &_pimpl->_dimsWithBorder[0]);
    }

    CloudsForm2D::~CloudsForm2D(){}

    CloudsForm2D::Settings::Settings()
    {
        _viscosity = .8f;
        _condensedDiffusionRate = 0.f;
        _vaporDiffusionRate = 0.1f;
        _temperatureDiffusionRate = 1.f;
        _diffusionMethod = 1;
        _advectionMethod = 3;
        _advectionSteps = 4;
        _enforceIncompressibilityMethod = 1;
        _vorticityConfinement = 0.4f;
        _interpolationMethod = 0;
        _buoyancyAlpha = 1.f;
        _buoyancyBeta = 1.f;                // getting interesting results with about a 30:1 ratio with buoyancy alpha (but the ideal ratio depends on _temperatureChangeSpeed
        _condensationSpeed = 1.f;
        _temperatureChangeSpeed = 1.f;      // some interesting results when over-emphasing this effect

        _crossWindSpeed = 5.f; // 30.f;
        _inputVapor = 1e-6f; // 0.0001f;
        _inputTemperature = .75f;
        _inputUpdraft = 1e-5f; // 0.06f;
        _evaporateThreshold = 0.95f;

        _airTemperature = CelsiusToKelvin(15.f);
        _relativeHumidity = .75f;
        _altitudeMin = 2.f;
        _altitudeMax = 4.f;
        _lapseRate = 6.5f;
        _obstructionType = 0;
    }
}

#include "../RenderOverlays/OverlayContext.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Utility/StringFormat.h"

namespace SceneEngine
{
    class WidgetResources
    {
    public:
		std::shared_ptr<RenderOverlays::Font> _font;
        WidgetResources();
    };

    WidgetResources::WidgetResources()
    {
        _font = RenderOverlays::GetX2Font("PoiretOne", 20u);
    }

    void CloudsForm2D::RenderWidgets(
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        using namespace RenderOverlays;
        using namespace RenderOverlays::DebuggingDisplay;

		auto overlayContext = MakeImmediateOverlayContext(context, &parserContext.GetNamedResources(), parserContext.GetProjectionDesc());
        overlayContext->CaptureState();

        TRY {

            auto maxCoords = context.GetStateDesc()._viewportDimensions;
            Rect rect(Coord2(0,0), Coord2(maxCoords[0], maxCoords[1]));
            Layout completeLayout(rect);

            auto& res = ConsoleRig::FindCachedBox2<WidgetResources>();
			TextStyle textStyle{};
            const auto lineHeight = 20u;

            UInt2 gridCoords(
                Clamp(unsigned(_pimpl->_mouseHover[0]), 0u, _pimpl->_dimsWithBorder[0]-1),
                Clamp(unsigned(_pimpl->_mouseHover[1]), 0u, _pimpl->_dimsWithBorder[1]-1));
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "At coords: " << gridCoords[0] << ", " << gridCoords[1]);

            const auto i = gridCoords[1]*_pimpl->_dimsWithBorder[0]+gridCoords[0];
            const auto qv = _pimpl->_vaporMixingRatio[1][i];
            const auto qc = _pimpl->_condensedMixingRatio[1][i];
            const auto potTemp = _pimpl->_potentialTemperature[1][i];
            const float equilibiriumVapor = _pimpl->_troposphere.GetEquilibriumMixingRatio(potTemp, gridCoords[1]);
            const float ambientTheta = _pimpl->_troposphere.GetPotentialTemperature(gridCoords[1]);
            const float ambientEquilib = _pimpl->_troposphere.GetEquilibriumMixingRatio(
                _pimpl->_troposphere.GetPotentialTemperature(gridCoords[1]), gridCoords[1]);
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "Vapour: " << qv << " Amb: " << _pimpl->_troposphere.GetVaporMixingRatio(gridCoords[1]));
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "%Equilib: " << 100.f * qv / equilibiriumVapor << "% RH: " << 100.f * qv / ambientEquilib << "%");
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "Condensed: " << qc);
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "Theta: " << KelvinToCelsius(potTemp) << " Buoyancy: " << potTemp/ambientTheta-1.f);

            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "Altitude: " << _pimpl->_troposphere.AltitudeKm(gridCoords[1]));
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "Equilibrium Vapor: " << equilibiriumVapor << " Amb: " << ambientEquilib);
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "Temp Release: " << _pimpl->_troposphere.GetPotentialTemperatureRelease(gridCoords[1]));
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() << "Ambient Theta: " << KelvinToCelsius(ambientTheta));
            overlayContext.DrawText(
                AsPixelCoords(completeLayout.AllocateFullWidth(lineHeight)),
                res._font, textStyle, ColorB(0xffffffff), TextAlignment::Left,
                StringMeld<256>() 
                    << "Pressure: " << PressureAtAltitude(_pimpl->_troposphere.AltitudeKm(gridCoords[1]), _pimpl->_troposphere.LapseRate())
                    << " Exner: " << ExnerFunction(PressureAtAltitude(_pimpl->_troposphere.AltitudeKm(gridCoords[1]), _pimpl->_troposphere.LapseRate())));


        } CATCH(const std::exception&) {
        } CATCH_END

        overlayContext->ReleaseState();
    }
}


template<> const ClassAccessors& Legacy_GetAccessors<SceneEngine::CloudsForm2D::Settings>()
{
    using Obj = SceneEngine::CloudsForm2D::Settings;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("Viscosity", DefaultGet(Obj, _viscosity),  DefaultSet(Obj, _viscosity));
        props.Add("CondensedDiffusionRate", DefaultGet(Obj, _condensedDiffusionRate),  DefaultSet(Obj, _condensedDiffusionRate));
        props.Add("VaporDiffusionRate", DefaultGet(Obj, _vaporDiffusionRate),  DefaultSet(Obj, _vaporDiffusionRate));
        props.Add("TemperatureDiffusionRate", DefaultGet(Obj, _temperatureDiffusionRate),  DefaultSet(Obj, _temperatureDiffusionRate));
        props.Add("DiffusionMethod", DefaultGet(Obj, _diffusionMethod),  DefaultSet(Obj, _diffusionMethod));

        props.Add("AdvectionMethod", DefaultGet(Obj, _advectionMethod),  DefaultSet(Obj, _advectionMethod));
        props.Add("AdvectionSteps", DefaultGet(Obj, _advectionSteps),  DefaultSet(Obj, _advectionSteps));
        props.Add("InterpolationMethod", DefaultGet(Obj, _interpolationMethod),  DefaultSet(Obj, _interpolationMethod));

        props.Add("EnforceIncompressibility", DefaultGet(Obj, _enforceIncompressibilityMethod),  DefaultSet(Obj, _enforceIncompressibilityMethod));
        props.Add("VorticityConfinement", DefaultGet(Obj, _vorticityConfinement),  DefaultSet(Obj, _vorticityConfinement));
        props.Add("BuoyancyAlpha", DefaultGet(Obj, _buoyancyAlpha),  DefaultSet(Obj, _buoyancyAlpha));
        props.Add("BuoyancyBeta", DefaultGet(Obj, _buoyancyBeta),  DefaultSet(Obj, _buoyancyBeta));

        props.Add("CrossWindSpeed", DefaultGet(Obj, _crossWindSpeed),  DefaultSet(Obj, _crossWindSpeed));
        props.Add("InputVapor", DefaultGet(Obj, _inputVapor),  DefaultSet(Obj, _inputVapor));
        props.Add("InputTemperature", DefaultGet(Obj, _inputTemperature),  DefaultSet(Obj, _inputTemperature));
        props.Add("InputUpdraft", DefaultGet(Obj, _inputUpdraft),  DefaultSet(Obj, _inputUpdraft));

        props.Add("CondensationSpeed", DefaultGet(Obj, _condensationSpeed),  DefaultSet(Obj, _condensationSpeed));
        props.Add("TemperatureChangeSpeed", DefaultGet(Obj, _temperatureChangeSpeed),  DefaultSet(Obj, _temperatureChangeSpeed));
        props.Add("EvaporateThreshold", DefaultGet(Obj, _evaporateThreshold),  DefaultSet(Obj, _evaporateThreshold));

        props.Add("AirTemperature", 
            [](const Obj& obj) { return SceneEngine::KelvinToCelsius(obj._airTemperature); },  
            [](Obj& obj, float value) { obj._airTemperature = SceneEngine::CelsiusToKelvin(value); });
        props.Add("RelativeHumidity", DefaultGet(Obj, _relativeHumidity),  DefaultSet(Obj, _relativeHumidity));
        props.Add("AltitudeMin", DefaultGet(Obj, _altitudeMin),  DefaultSet(Obj, _altitudeMin));
        props.Add("AltitudeMax", DefaultGet(Obj, _altitudeMax),  DefaultSet(Obj, _altitudeMax));
        props.Add("LapseRate", DefaultGet(Obj, _lapseRate),  DefaultSet(Obj, _lapseRate));
        props.Add("ObstructionType", DefaultGet(Obj, _obstructionType),  DefaultSet(Obj, _obstructionType));
        
        init = true;
    }
    return props;
}


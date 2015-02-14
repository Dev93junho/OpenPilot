/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup PathFollower CONTROL interface class
 * @brief CONTROL interface class for pathfollower goal fsm implementations
 * @{
 *
 * @file       PIDControlThrust.h
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2015.
 * @brief      Executes CONTROL for landing sequence
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
extern "C" {
#include <openpilot.h>

#include <callbackinfo.h>

#include <math.h>
#include <pid.h>
#include <CoordinateConversions.h>
#include <sin_lookup.h>
#include <pathdesired.h>
#include <paths.h>
#include "plans.h"
#include <stabilizationdesired.h>
}

#include "PathFollowerFSM.h"
#include "PIDControlThrust.h"

PIDControlThrust::PIDControlThrust()
    : deltaTime(0), mVelocitySetpointTarget(0), mVelocityState(0), mThrustCommand(0.0f), mFSM(0), mNeutral(0.5f), mActive(false)
{
    Deactivate();
}

PIDControlThrust::~PIDControlThrust() {}

void PIDControlThrust::Initialize(PathFollowerFSM *fsm)
{
    mFSM = fsm;
}

void PIDControlThrust::Deactivate()
{
    // pid_zero(&PID);
    mActive = false;
}

void PIDControlThrust::Activate()
{
    float currentThrust;

    StabilizationDesiredThrustGet(&currentThrust);
    float u0 = currentThrust - mNeutral;
    pid2_transfer(&PID, u0);
    mActive = true;
}

void PIDControlThrust::UpdateParameters(float kp, float ki, float kd, __attribute__((unused)) float ilimit, float dT, float velocityMax)
{
    // pid_configure(&PID, kp, ki, kd, ilimit);
    float Ti   = kp / ki;
    float Td   = kd / kp;
    float kt   = (Ti + Td) / 2.0f;
    float Tf   = Td / 10.0f;
    float beta = 1.0f; // 0 to 1
    float u0   = 0.0f;

    pid2_configure(&PID, kp, ki, kd, Tf, kt, dT, beta, u0);
    deltaTime    = dT;
    mVelocityMax = velocityMax;
}

void PIDControlThrust::UpdateNeutralThrust(float neutral)
{
    if (mActive) {
        // adjust neutral and achieve bumpless transfer
        PID.I += mNeutral - neutral;
    }
    mNeutral = neutral;
}

void PIDControlThrust::UpdateVelocitySetpoint(float setpoint)
{
    mVelocitySetpointTarget = setpoint;
    if (fabsf(mVelocitySetpointTarget) > mVelocityMax) {
        // maintain sign but set to max
        mVelocitySetpointTarget *= mVelocityMax / fabsf(mVelocitySetpointTarget);
    }
}

void PIDControlThrust::RateLimit(float *spDesired, float *spCurrent, float rateLimit)
{
    float velocity_delta = *spDesired - *spCurrent;

    if (fabsf(velocity_delta) < 1e-6f) {
        *spCurrent = *spDesired;
        return;
    }

    // Calculate the rate of change
    float accelerationDesired = velocity_delta / deltaTime;

    if (fabsf(accelerationDesired) > rateLimit) {
        accelerationDesired *= rateLimit / accelerationDesired;
    }

    if (fabsf(accelerationDesired) < 0.1f) {
        *spCurrent = *spDesired;
    } else {
        *spCurrent += accelerationDesired * deltaTime;
    }
}

// Update velocity state called per dT. Also update current
// desired velocity
void PIDControlThrust::UpdateVelocityState(float pv)
{
    mVelocityState = pv;

    // The FSM controls the actual descent velocity and introduces step changes as required
    float velocitySetpointDesired = mFSM->BoundVelocityDown(mVelocitySetpointTarget);
    // RateLimit(velocitySetpointDesired, mVelocitySetpointCurrent, 2.0f );
    mVelocitySetpointCurrent = velocitySetpointDesired;
}

float PIDControlThrust::GetVelocityDesired(void)
{
    return mVelocitySetpointCurrent;
}

float PIDControlThrust::GetThrustCommand(void)
{
    // pid_scaler local_scaler = { .p = 1.0f, .i = 1.0f, .d = 1.0f };
    // mFSM->CheckPidScaler(&local_scaler);
    // float downCommand    = -pid_apply_setpoint(&PID, &local_scaler, mVelocitySetpoint, mState, deltaTime);
    float ulow, uhigh;

    mFSM->BoundThrust(ulow, uhigh);
    float downCommand = -pid2_apply(&PID, mVelocitySetpointCurrent, mVelocityState, ulow - mNeutral, uhigh - mNeutral);
    mThrustCommand = mNeutral + downCommand;
    return mThrustCommand;
}
/*
 * amplifier.c
 *
 *  Created on: Jul 14, 2014
 *      Author: artem
 */

#include <stdlib.h>
#include <string.h>

#include "amplifier.h"

void AmplifierInit(AmplifierTopology_t* pTop, AmplifierCoefficients_t* pCoefs, AmplifierState_t* pState)
{
	memset(pTop,0,sizeof(AmplifierTopology_t));
	memset(pCoefs,0,sizeof(AmplifierCoefficients_t));
	memset(pState,0,sizeof(AmplifierState_t));
}

void AmplifierProcess(
	AmplifierTopology_t* pTop,
	AmplifierCoefficients_t* pCoefs,
	AmplifierState_t* pState,
	int32_t* pInput, int32_t* pOutput, int32_t nLength)
{

	pState->mInputLeftLevelMeter = abs(rand());
	pState->mInputRightLevelMeter = abs(rand());
}

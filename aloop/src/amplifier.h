/*
 * amplifier.h
 *
 *  Created on: Jul 14, 2014
 *      Author: artem
 */

#ifndef AMPLIFIER_H_
#define AMPLIFIER_H_

#include <stdint.h>
#include "amplifierstate.h"
#include "amplifiertopology.h"

typedef struct
{
	int32_t mActive;

}	AmplifierCoefficients_t;

void AmplifierInit(AmplifierTopology_t* pTop, AmplifierCoefficients_t* pCoefs, AmplifierState_t* pState);

void AmplifierProcess(
	AmplifierTopology_t* pTop,
	AmplifierCoefficients_t* pCoefs,
	AmplifierState_t* pState,
	int32_t* pInput, int32_t* pOutput, int32_t nLength);

#endif /* AMPLIFIER_H_ */

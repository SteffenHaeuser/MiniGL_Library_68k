/*
 * (C) 2025-2026 Dennis van der Boon
 *
 * This file is part of the MiniGLV3D library project
 * See the file Licence.txt for more details
 */

/*
 * GL 1.1 fixed-function lighting: the state half.
 *
 * New in MiniGLV3D -- base MiniGL has no lighting at all, so there is no
 * original to fork here. The split between this file and the shader is the
 * design's main decision and worth stating once:
 *
 *   - THIS FILE owns the state and the FOLD. Everything that depends only on
 *     what glLight, glMaterial and glLightModel were called with is computed
 *     once, when it is set, and stored. That is the light x material products,
 *     which GL multiplies per vertex but which cannot change between vertices.
 *
 *   - THE PER-DRAW PATH (draw.c) owns only what depends on the matrix: carrying
 *     each enabled light into object space, and writing the folded values into
 *     the lit shader's uniform tail.
 *
 *   - THE QPU VERTEX SHADER owns the per-vertex arithmetic: the dot product, the
 *     clamp at zero, and the accumulation.
 *
 * So a lit frame that changes no lighting state does no folding at all, and a
 * lit frame that changes no matrix rewrites no uniform block either.
 *
 * TWO PLACES THIS FILE IS DELIBERATELY MORE COMPLETE THAN THE SHADER, because
 * the alternative is worse:
 *
 *   - Spot and attenuation parameters are stored and answered by glGetLightfv
 *     even though nothing consumes them yet. A parameter dropped on the way in
 *     cannot be answered later.
 *
 *   - All eight lights exist. The demo that drove this work uses exactly one,
 *     but the state is the cheap half and a second light later is then a shader
 *     variant and nothing else.
 *
 * WHAT IS NOT HERE: GL_LIGHT_MODEL_TWO_SIDE, which is out of reach rather than
 * unfinished -- there is no front/back varying select at this vertex stage, so
 * a back-facing vertex cannot be given a different material. Its token is
 * accepted and stored so that setting it is not an error, and never read.
 */

#include "sysinc.h"

/*
 * GL's real values for the face tokens. GL_FRONT, GL_BACK and GL_FRONT_AND_BACK
 * are POSITIONAL members of gl.h's enum in this driver, so an application built
 * against this header passes a small number while one built against standard GL
 * headers passes these. Both are accepted, exactly as draw.c accepts both
 * spellings of GL_SPHERE_MAP. The positional members stop at 240, so the two
 * numberings cannot collide and no ordering between the tests matters.
 */
#define MGL_FRONT_SPEC          0x0404
#define MGL_BACK_SPEC           0x0405
#define MGL_FRONT_AND_BACK_SPEC 0x0408

/*
 * GL_LIGHTi -> 0..7, or -1 for anything else.
 *
 * Both spellings again, and here the positional one needs a word: GL_LIGHT0 is
 * 0x4000 and there is no positional alias, because lighting never existed in
 * this header before. So only the real values are legal, and the range test is
 * the whole validation.
 */
static int light_Index(GLenum light)
{
	if (light >= GL_LIGHT0 && light < (GLenum)(GL_LIGHT0 + MGL_MAX_LIGHTS))
		return (int)(light - GL_LIGHT0);

	return -1;
}

/*
 * Which material the call addresses. This driver has ONE material, not the
 * front/back pair GL defines, because there is no way to give a back-facing
 * vertex a different colour at this vertex stage (see gl.h). GL_BACK is
 * therefore accepted and applied to the single material rather than rejected:
 * rejecting it would set GL_INVALID_ENUM on a perfectly ordinary call, and an
 * application that checks glGetError can treat a pending error as fatal.
 *
 * Returns GL_TRUE for any legal face token, GL_FALSE for a bad one.
 */
static GLboolean light_IsFace(GLenum face)
{
	return (face == GL_FRONT || face == GL_BACK || face == GL_FRONT_AND_BACK
	     || face == MGL_FRONT_SPEC || face == MGL_BACK_SPEC
	     || face == MGL_FRONT_AND_BACK_SPEC) ? GL_TRUE : GL_FALSE;
}

static void light_Copy4(GLfloat *dst, const GLfloat *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
}

/*
 * The fold. Recomputed whenever any lighting state changes, which is why the
 * per-draw path can write the uniform tail with no arithmetic beyond the
 * object-space transform.
 *
 * LitBase collects every term that does not depend on a normal:
 *
 *     emission + light_model_ambient * material_ambient
 *              + SUM over ENABLED lights of light_ambient * material_ambient
 *
 * which is GL's lit-colour equation with the diffuse and specular sums removed.
 * Its alpha is the material's DIFFUSE alpha -- not the ambient alpha and not a
 * sum -- because that is the alpha GL gives a lit vertex.
 *
 * LitDiffuse[i] is light i's diffuse times the material diffuse. It is indexed
 * by LIGHT NUMBER, not by uniform-tail slot: the compaction into slots happens
 * when the tail is written, so that a disabled light between two enabled ones
 * costs a tail slot in neither, while this array stays readable by light number.
 *
 * No clamping happens here. GL clamps the FINAL vertex colour to [0,1], and
 * clamping the products early would darken a scene whose individual terms
 * legitimately exceed 1 while their sum does not.
 */
void light_Fold(GLcontext context)
{
	const MGLMaterial *m = &context->Material;
	/* GL_COLOR_MATERIAL replaces the named material component with the vertex
	 * colour. Every term it touches stays LINEAR in that colour, so each one
	 * folds to K0 + K1 * C: the tracked component is taken as ZERO in K0 and
	 * its light-side factor becomes K1. With colour material off every K1 is
	 * zero and K0 is exactly what this function always computed. */
	GLboolean on = context->ColorMaterial_State;
	GLenum    md = context->ColorMaterialMode;
	GLboolean tE = (GLboolean)(on && md == GL_EMISSION);
	GLboolean tA = (GLboolean)(on && (md == GL_AMBIENT || md == GL_AMBIENT_AND_DIFFUSE));
	GLboolean tD = (GLboolean)(on && (md == GL_DIFFUSE || md == GL_AMBIENT_AND_DIFFUSE));
	GLboolean tS = (GLboolean)(on && md == GL_SPECULAR);
	int i, c;

	for (c = 0; c < 3; c++)
	{
		context->LitBase[c]  = (tE ? 0.0f : m->Emission[c])
		                     + context->LightModelAmbient[c] * (tA ? 0.0f : m->Ambient[c]);
		context->LitBaseC[c] = (tE ? 1.0f : 0.0f)
		                     + (tA ? context->LightModelAmbient[c] : 0.0f);
	}
	/* GL gives a lit vertex the material DIFFUSE alpha, so tracking diffuse
	 * makes the vertex colour's own alpha the answer. */
	context->LitBase[3]  = tD ? 0.0f : m->Diffuse[3];
	context->LitBaseC[3] = tD ? 1.0f : 0.0f;

	for (i = 0; i < MGL_MAX_LIGHTS; i++)
	{
		const MGLLight *L = &context->Light[i];

		if ((context->LightMask & (1U << i)) == 0)
		{
			/* A disabled light contributes nothing, and its folded diffuse
			 * is zeroed rather than left stale: the tail writer reads this
			 * array by light number and a light re-enabled later must not
			 * pick up a product folded against an older material. */
			for (c = 0; c < 3; c++)
			{
				context->LitDiffuse[i][c]   = 0.0f;
				context->LitSpecular[i][c]  = 0.0f;
				context->LitDiffuseC[i][c]  = 0.0f;
				context->LitSpecularC[i][c] = 0.0f;
			}
			continue;
		}

		for (c = 0; c < 3; c++)
		{
			context->LitBase[c]        += L->Ambient[c] * (tA ? 0.0f : m->Ambient[c]);
			context->LitBaseC[c]       += tA ? L->Ambient[c] : 0.0f;
			context->LitDiffuse[i][c]   = L->Diffuse[c]  * (tD ? 0.0f : m->Diffuse[c]);
			context->LitDiffuseC[i][c]  = tD ? L->Diffuse[c]  : 0.0f;
			context->LitSpecular[i][c]  = L->Specular[c] * (tS ? 0.0f : m->Specular[c]);
			context->LitSpecularC[i][c] = tS ? L->Specular[c] : 0.0f;
		}
	}
}

/*
 * GL 1.1 defaults, called once per context. Two of these are the kind of detail
 * that renders a whole scene black if it is assumed instead of read:
 *
 *   - LIGHT 0 IS SPECIAL. Its default diffuse and specular are (1,1,1,1); every
 *     other light's are (0,0,0,1). So a light the application enables without
 *     configuring lights nothing unless it happens to be light 0, and an
 *     implementation that gave all eight lights light 0's defaults would light
 *     scenes that GL leaves dark.
 *
 *   - The default POSITION is (0,0,1,0): w == 0, so the default light is
 *     DIRECTIONAL, shining down -z. Defaulting it to (0,0,1,1) instead would
 *     make it a point light one unit from the eye.
 */
void light_SetDefaults(GLcontext context)
{
	int i;

	context->Lighting_State = GL_FALSE;
	context->LightMask      = 0;
	context->LightSerial    = 0;

	for (i = 0; i < MGL_MAX_LIGHTS; i++)
	{
		MGLLight *L = &context->Light[i];

		L->Ambient[0] = 0.0f; L->Ambient[1] = 0.0f;
		L->Ambient[2] = 0.0f; L->Ambient[3] = 1.0f;

		if (i == 0)
		{
			L->Diffuse[0]  = 1.0f; L->Diffuse[1]  = 1.0f;
			L->Diffuse[2]  = 1.0f; L->Diffuse[3]  = 1.0f;
			L->Specular[0] = 1.0f; L->Specular[1] = 1.0f;
			L->Specular[2] = 1.0f; L->Specular[3] = 1.0f;
		}
		else
		{
			L->Diffuse[0]  = 0.0f; L->Diffuse[1]  = 0.0f;
			L->Diffuse[2]  = 0.0f; L->Diffuse[3]  = 1.0f;
			L->Specular[0] = 0.0f; L->Specular[1] = 0.0f;
			L->Specular[2] = 0.0f; L->Specular[3] = 1.0f;
		}

		L->Position[0] = 0.0f; L->Position[1] = 0.0f;
		L->Position[2] = 1.0f; L->Position[3] = 0.0f;

		L->SpotDirection[0] =  0.0f;
		L->SpotDirection[1] =  0.0f;
		L->SpotDirection[2] = -1.0f;
		L->SpotExponent     =  0.0f;
		L->SpotCutoff       =  180.0f;

		L->ConstantAttenuation  = 1.0f;
		L->LinearAttenuation    = 0.0f;
		L->QuadraticAttenuation = 0.0f;
	}

	context->Material.Ambient[0]  = 0.2f; context->Material.Ambient[1]  = 0.2f;
	context->Material.Ambient[2]  = 0.2f; context->Material.Ambient[3]  = 1.0f;
	context->Material.Diffuse[0]  = 0.8f; context->Material.Diffuse[1]  = 0.8f;
	context->Material.Diffuse[2]  = 0.8f; context->Material.Diffuse[3]  = 1.0f;
	context->Material.Specular[0] = 0.0f; context->Material.Specular[1] = 0.0f;
	context->Material.Specular[2] = 0.0f; context->Material.Specular[3] = 1.0f;
	context->Material.Emission[0] = 0.0f; context->Material.Emission[1] = 0.0f;
	context->Material.Emission[2] = 0.0f; context->Material.Emission[3] = 1.0f;
	context->Material.Shininess   = 0.0f;

	context->LightModelAmbient[0] = 0.2f;
	context->LightModelAmbient[1] = 0.2f;
	context->LightModelAmbient[2] = 0.2f;
	context->LightModelAmbient[3] = 1.0f;
	context->LightModelTwoSide    = GL_FALSE;
	context->LightModelLocalViewer = GL_FALSE;

	/* GL 1.1's own defaults: off, GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE.
	 * The mode matters even while disabled, because an application may call
	 * glColorMaterial once and enable it later. */
	context->ColorMaterial_State = GL_FALSE;
	context->ColorMaterialFace   = GL_FRONT_AND_BACK;
	context->ColorMaterialMode   = GL_AMBIENT_AND_DIFFUSE;

	light_Fold(context);
}

void GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params)
{
	int      n = light_Index(light);
	MGLLight *L;

	if (n < 0 || params == NULL)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	L = &context->Light[n];
	context->LightSerial++;

	switch (pname)
	{
		case GL_AMBIENT:
			light_Copy4(L->Ambient, params);
			break;
		case GL_DIFFUSE:
			light_Copy4(L->Diffuse, params);
			break;
		case GL_SPECULAR:
			light_Copy4(L->Specular, params);
			break;

		/*
		 * GL_POSITION is transformed by the MODELVIEW MATRIX IN FORCE NOW and
		 * stored in EYE coordinates. This is GL, not an optimisation: the light
		 * does not follow the modelview afterwards, so an application that sets
		 * its light under the camera transform and then rotates an object gets a
		 * light fixed in the world, which is what every scene expects.
		 *
		 * The 4th component is transformed too, not forced. A directional light
		 * has w == 0, and a 4x4 modelview applied to (x,y,z,0) correctly ignores
		 * the translation column -- so one expression serves both light types
		 * and the type survives the transform.
		 */
		case GL_POSITION:
		{
			#define a(x) (CurrentMV->v[OF_##x])

			L->Position[0] = a(11)*params[0] + a(12)*params[1]
			               + a(13)*params[2] + a(14)*params[3];
			L->Position[1] = a(21)*params[0] + a(22)*params[1]
			               + a(23)*params[2] + a(24)*params[3];
			L->Position[2] = a(31)*params[0] + a(32)*params[1]
			               + a(33)*params[2] + a(34)*params[3];
			L->Position[3] = a(41)*params[0] + a(42)*params[1]
			               + a(43)*params[2] + a(44)*params[3];

			#undef a
			break;
		}

		/*
		 * The spot direction is a DIRECTION, so it takes the upper 3x3 of the
		 * modelview and not the translation -- the same distinction the w == 0
		 * case of GL_POSITION above handles arithmetically.
		 */
		case GL_SPOT_DIRECTION:
		{
			#define a(x) (CurrentMV->v[OF_##x])

			L->SpotDirection[0] = a(11)*params[0] + a(12)*params[1] + a(13)*params[2];
			L->SpotDirection[1] = a(21)*params[0] + a(22)*params[1] + a(23)*params[2];
			L->SpotDirection[2] = a(31)*params[0] + a(32)*params[1] + a(33)*params[2];

			#undef a
			break;
		}

		case GL_SPOT_EXPONENT:
		case GL_SPOT_CUTOFF:
		case GL_CONSTANT_ATTENUATION:
		case GL_LINEAR_ATTENUATION:
		case GL_QUADRATIC_ATTENUATION:
			GLLightf(context, light, pname, params[0]);
			return;                 /* GLLightf folds and bumps the serial */

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}

	light_Fold(context);
}

void GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param)
{
	int      n = light_Index(light);
	MGLLight *L;

	if (n < 0)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	L = &context->Light[n];
	context->LightSerial++;

	switch (pname)
	{
		/* Stored, never read -- no spot or attenuation term exists in the lit
		 * shader yet. Stored anyway so glGetLightfv answers truthfully; see
		 * this file's header comment. */
		case GL_SPOT_EXPONENT:
			L->SpotExponent = param;
			break;
		case GL_SPOT_CUTOFF:
			L->SpotCutoff = param;
			break;
		case GL_CONSTANT_ATTENUATION:
			L->ConstantAttenuation = param;
			break;
		case GL_LINEAR_ATTENUATION:
			L->LinearAttenuation = param;
			break;
		case GL_QUADRATIC_ATTENUATION:
			L->QuadraticAttenuation = param;
			break;

		default:
			/* GL_AMBIENT and the rest are 4-component parameters; GL says the
			 * scalar entry point on one of those is GL_INVALID_ENUM. */
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}

	light_Fold(context);
}

/*
 * GL_COLOR_MATERIAL's parameter. The face is stored only so glGet can answer
 * GL_COLOR_MATERIAL_FACE -- there is one material here, not GL's pair.
 *
 * GL_SHININESS is deliberately not accepted: GL names five modes for this call
 * and shininess is not one of them, a scalar having no vertex colour to track.
 */
void GLColorMaterial(GLcontext context, GLenum face, GLenum mode)
{
	if (light_IsFace(face) == GL_FALSE)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	switch (mode)
	{
		case GL_EMISSION:
		case GL_AMBIENT:
		case GL_DIFFUSE:
		case GL_SPECULAR:
		case GL_AMBIENT_AND_DIFFUSE:
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}

	context->ColorMaterialFace = face;
	context->ColorMaterialMode = mode;
	context->LightSerial++;
	light_Fold(context);
}

void GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params)
{
	if (light_IsFace(face) == GL_FALSE || params == NULL)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	context->LightSerial++;

	switch (pname)
	{
		case GL_AMBIENT:
			light_Copy4(context->Material.Ambient, params);
			break;
		case GL_DIFFUSE:
			light_Copy4(context->Material.Diffuse, params);
			break;
		case GL_SPECULAR:
			light_Copy4(context->Material.Specular, params);
			break;
		case GL_EMISSION:
			light_Copy4(context->Material.Emission, params);
			break;

		/* One call setting both, which is what GL_COLOR_MATERIAL's default mode
		 * also names. Cheaper to honour here than to make the application issue
		 * two calls. */
		case GL_AMBIENT_AND_DIFFUSE:
			light_Copy4(context->Material.Ambient, params);
			light_Copy4(context->Material.Diffuse, params);
			break;

		case GL_SHININESS:
			GLMaterialf(context, face, pname, params[0]);
			return;                 /* GLMaterialf folds and bumps the serial */

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			return;
	}

	light_Fold(context);
}

void GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param)
{
	if (light_IsFace(face) == GL_FALSE)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	if (pname != GL_SHININESS)
	{
		/* Everything else on the material is a 4-component parameter. */
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	/*
	 * Clamped to GL's [0,128] because a
	 * value outside it is GL_INVALID_VALUE, and flagging that is more useful
	 * than storing a number the shader would later have to defend against.
	 */
	if (param < 0.0f || param > 128.0f)
	{
		GLFlagError(context, 1, GL_INVALID_VALUE);
		return;
	}

	context->LightSerial++;
	context->Material.Shininess = param;
	light_Fold(context);
}

void GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params)
{
	if (params == NULL)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	switch (pname)
	{
		case GL_LIGHT_MODEL_AMBIENT:
			context->LightSerial++;
			light_Copy4(context->LightModelAmbient, params);
			light_Fold(context);
			break;

		case GL_LIGHT_MODEL_TWO_SIDE:
		case GL_LIGHT_MODEL_LOCAL_VIEWER:
			GLLightModelf(context, pname, params[0]);
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}

void GLLightModelf(GLcontext context, GLenum pname, GLfloat param)
{
	switch (pname)
	{
		/* Both stored, neither read. Two-sided lighting is out of reach at this
		 * vertex stage and the local-viewer distinction only affects specular,
		 * which the shader does not compute yet. Accepting them keeps an
		 * ordinary glLightModeli call from setting a sticky GL_INVALID_ENUM. */
		case GL_LIGHT_MODEL_TWO_SIDE:
			context->LightModelTwoSide = (param != 0.0f) ? GL_TRUE : GL_FALSE;
			break;
		case GL_LIGHT_MODEL_LOCAL_VIEWER:
			context->LightModelLocalViewer = (param != 0.0f) ? GL_TRUE : GL_FALSE;
			break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}

/*
 * The queries. GL_POSITION answers in EYE coordinates, which is what is stored
 * and what GL specifies glGetLightfv returns -- it does NOT undo the modelview
 * transform to hand back the application's own numbers.
 */
void GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params)
{
	int            n = light_Index(light);
	const MGLLight *L;

	if (n < 0 || params == NULL)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	L = &context->Light[n];

	switch (pname)
	{
		case GL_AMBIENT:    light_Copy4(params, L->Ambient);  break;
		case GL_DIFFUSE:    light_Copy4(params, L->Diffuse);  break;
		case GL_SPECULAR:   light_Copy4(params, L->Specular); break;
		case GL_POSITION:   light_Copy4(params, L->Position); break;

		case GL_SPOT_DIRECTION:
			params[0] = L->SpotDirection[0];
			params[1] = L->SpotDirection[1];
			params[2] = L->SpotDirection[2];
			break;

		case GL_SPOT_EXPONENT:           params[0] = L->SpotExponent;        break;
		case GL_SPOT_CUTOFF:             params[0] = L->SpotCutoff;          break;
		case GL_CONSTANT_ATTENUATION:    params[0] = L->ConstantAttenuation; break;
		case GL_LINEAR_ATTENUATION:      params[0] = L->LinearAttenuation;   break;
		case GL_QUADRATIC_ATTENUATION:   params[0] = L->QuadraticAttenuation; break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}

void GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params)
{
	if (light_IsFace(face) == GL_FALSE || params == NULL)
	{
		GLFlagError(context, 1, GL_INVALID_ENUM);
		return;
	}

	switch (pname)
	{
		case GL_AMBIENT:   light_Copy4(params, context->Material.Ambient);  break;
		case GL_DIFFUSE:   light_Copy4(params, context->Material.Diffuse);  break;
		case GL_SPECULAR:  light_Copy4(params, context->Material.Specular); break;
		case GL_EMISSION:  light_Copy4(params, context->Material.Emission); break;
		case GL_SHININESS: params[0] = context->Material.Shininess;         break;

		default:
			GLFlagError(context, 1, GL_INVALID_ENUM);
			break;
	}
}

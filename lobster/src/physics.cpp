// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#include "lobster/glinterface.h"

#include "Box2D/Box2D.h"

using namespace lobster;

struct Renderable : Textured {
	float4 color;
	Shader *sh;

	Renderable(const char *shname) : color(float4_1), sh(LookupShader(shname)) {
		assert(sh);
	}

	void Set() {
		sh->Set();
		sh->SetTextures(textures);
	}
};

b2World *world = nullptr;
b2ParticleSystem *particlesystem = nullptr;
Renderable *particlematerial = nullptr;

b2Vec2 Float2ToB2(const float2 &v) { return b2Vec2(v.x, v.y); }
float2 B2ToFloat2(const b2Vec2 &v) { return float2(v.x, v.y); }

b2Vec2 ValueDecToB2(VM &vm, Value &vec) {
    auto v = ValueDecToFLT<2>(vm, vec);
    return Float2ToB2(v);
}

struct PhysicsObject {
    Renderable r;
    b2Fixture *fixture;
    vector<int> *particle_contacts;

    PhysicsObject(const Renderable &_r,  b2Fixture *_f)
        : r(_r), fixture(_f), particle_contacts(nullptr) {}
    ~PhysicsObject() {
        auto body = fixture->GetBody();
        body->DestroyFixture(fixture);
        if (!body->GetFixtureList()) world->DestroyBody(body);
        if (particle_contacts) delete particle_contacts;
    }
    float2 Pos() { return B2ToFloat2(fixture->GetBody()->GetPosition()); }
};

static ResourceType physics_type = { "physical", [](void *v) { delete ((PhysicsObject *)v); } };

PhysicsObject &GetObject(VM &vm, Value &res) {
    return *GetResourceDec<PhysicsObject *>(vm, res, &physics_type);
}

void CleanPhysics() {
	if (world) delete world;
	world = nullptr;
	particlesystem = nullptr;
	delete particlematerial;
	particlematerial = nullptr;
}

void InitPhysics(const float2 &gv) {
	// FIXME: check that shaders are initialized, since renderables depend on that
	CleanPhysics();
	world = new b2World(b2Vec2(gv.x, gv.y));
}

void CheckPhysics() {
	if (!world) InitPhysics(float2(0, -10));
}

void CheckParticles(float size = 0.1f) {
	CheckPhysics();
	if (!particlesystem) {
		b2ParticleSystemDef psd;
		psd.radius = size;
		particlesystem = world->CreateParticleSystem(&psd);
		particlematerial = new Renderable("color_attr");
	}
}

b2Body &GetBody(VM &vm, Value &id, Value &position) {
	CheckPhysics();
	b2Body *body = id.True() ? GetObject(vm, id).fixture->GetBody() : nullptr;
	auto wpos = ValueDecToFLT<2>(vm, position);
	if (!body) {
		b2BodyDef bd;
		bd.type = b2_staticBody;
		bd.position.Set(wpos.x, wpos.y);
		body = world->CreateBody(&bd);
	}
	return *body;
}

Value CreateFixture(VM &vm, b2Body &body, b2Shape &shape) {
	auto fixture = body.CreateFixture(&shape, 1.0f);
    auto po = new PhysicsObject(Renderable("color"), fixture);
	fixture->SetUserData(po);
	return Value(vm.NewResource(po, &physics_type));
}

b2Vec2 OptionalOffset(VM &vm, Value &offset) { return offset.True() ? ValueDecToB2(vm, offset) : b2Vec2_zero; }

Renderable &GetRenderable(VM &vm, Value &id) {
	CheckPhysics();
	return id.True() ? GetObject(vm, id).r : *particlematerial;
}

extern int GetSampler(VM &vm, Value &i); // from graphics

void AddPhysics(NativeRegistry &natreg) {
	STARTDECL(ph_initialize) (VM &vm, Value &gravity) {
		InitPhysics(ValueDecToFLT<2>(vm, gravity));
		return Value();
	}
	ENDDECL1(ph_initialize, "gravityvector", "F}:2", "",
        "initializes or resets the physical world, gravity typically [0, -10].");

	STARTDECL(ph_createbox) (VM &vm, Value &position, Value &size, Value &offset, Value &rot,
                             Value &other_id) {
		auto &body = GetBody(vm, other_id, position);
		auto sz = ValueDecToFLT<2>(vm, size);
		auto r = rot.fltval();
		b2PolygonShape shape;
		shape.SetAsBox(sz.x, sz.y, OptionalOffset(vm, offset), r * RAD);
		return CreateFixture(vm, body, shape);
	}
	ENDDECL5(ph_createbox, "position,size,offset,rotation,attachto", "F}:2F}:2F}:2?F?X?", "X",
        "creates a physical box shape in the world at position, with size the half-extends around"
        " the center, offset from the center if needed, at a particular rotation (in degrees)."
        " attachto is a previous physical object to attach this one to, to become a combined"
        " physical body.");

	STARTDECL(ph_createcircle) (VM &vm, Value &position, Value &radius, Value &offset, Value &other_id) {
		auto &body = GetBody(vm, other_id, position);
		b2CircleShape shape;
		auto off = OptionalOffset(vm, offset);
		shape.m_p.Set(off.x, off.y);
		shape.m_radius = radius.fltval();
		return CreateFixture(vm, body, shape);
	}
	ENDDECL4(ph_createcircle, "position,radius,offset,attachto", "F}:2FF}:2?X?", "X",
        "creates a physical circle shape in the world at position, with the given radius, offset"
        " from the center if needed. attachto is a previous physical object to attach this one to,"
        " to become a combined physical body.");

	STARTDECL(ph_createpolygon) (VM &vm, Value &position, Value &vertices, Value &other_id) {
		auto &body = GetBody(vm, other_id, position);
        b2PolygonShape shape;
		auto verts = new b2Vec2[vertices.vval()->len];
        for (int i = 0; i < vertices.vval()->len; i++) {
            auto vert = ValueToFLT<2>(vm, vertices.vval()->At(i));
            verts[i] = Float2ToB2(vert);
        }
		shape.Set(verts, (int)vertices.vval()->len);
		delete[] verts;
		vertices.DECRT(vm);
		return CreateFixture(vm, body, shape);
	}
	ENDDECL3(ph_createpolygon, "position,vertices,attachto", "F}:2F}:2]X?", "X",
        "creates a polygon circle shape in the world at position, with the given list of vertices."
        " attachto is a previous physical object to attach this one to, to become a combined"
        " physical body.");

	STARTDECL(ph_dynamic) (VM &vm, Value &fixture_id, Value &on) {
		CheckPhysics();
        GetObject(vm, fixture_id).fixture->GetBody()->SetType(on.ival() ? b2_dynamicBody
                                                                    : b2_staticBody);
		return Value();
	}
	ENDDECL2(ph_dynamic, "shape,on", "XI", "",
        "makes a shape dynamic (on = true) or not.");

	STARTDECL(ph_setcolor) (VM &vm, Value &fixture_id, Value &color) {
		auto &r = GetRenderable(vm, fixture_id);
		auto c = ValueDecToFLT<4>(vm, color);
		r.color = c;
		return Value();
	}
	ENDDECL2(ph_setcolor, "id,color", "X?F}:4", "",
        "sets a shape (or nil for particles) to be rendered with a particular color.");

	STARTDECL(ph_setshader) (VM &vm, Value &fixture_id, Value &shader) {
		auto &r = GetRenderable(vm, fixture_id);
		auto sh = LookupShader(shader.sval()->str());
		shader.DECRT(vm);
		if (sh) r.sh = sh;
		return Value();
	}
	ENDDECL2(ph_setshader, "id,shadername", "X?S", "",
        "sets a shape (or nil for particles) to be rendered with a particular shader.");

	STARTDECL(ph_settexture) (VM &vm, Value &fixture_id, Value &tex, Value &tex_unit) {
		auto &r = GetRenderable(vm, fixture_id);
        extern Texture GetTexture(VM &vm, Value &res);
		r.Get(GetSampler(vm, tex_unit)) = GetTexture(vm, tex);
		return Value();
	}
	ENDDECL3(ph_settexture, "id,tex,texunit", "X?XI?", "",
        "sets a shape (or nil for particles) to be rendered with a particular texture"
        " (assigned to a texture unit, default 0).");

    STARTDECL(ph_getposition) (VM &vm, Value &fixture_id) {
        return Value(ToValueFLT(vm, GetObject(vm, fixture_id).Pos()));
    }
    ENDDECL1(ph_getposition, "id", "X", "F}:2",
             "gets a shape's position.");

    STARTDECL(ph_createparticle) (VM &vm, Value &position, Value &velocity, Value &color, Value &type) {
        CheckParticles();
        b2ParticleDef pd;
        pd.flags = type.intval();
        auto c = ValueDecToFLT<3>(vm, color);
        pd.color.Set(b2Color(c.x, c.y, c.z));
        pd.position = ValueDecToB2(vm, position);
        pd.velocity = ValueDecToB2(vm, velocity);
        return Value(particlesystem->CreateParticle(pd));
    }
    ENDDECL4(ph_createparticle, "position,velocity,color,flags", "F}:2F}:2F}:4I?", "I",
        "creates an individual particle. For flags, see include/physics.lobster");

	STARTDECL(ph_createparticlecircle) (VM &vm, Value &position, Value &radius, Value &color, Value &type) {
		CheckParticles();
		b2ParticleGroupDef pgd;
		b2CircleShape shape;
		shape.m_radius = radius.fltval();
		pgd.shape = &shape;
		pgd.flags = type.intval();
		pgd.position = ValueDecToB2(vm, position);
		auto c = ValueDecToFLT<3>(vm, color);
		pgd.color.Set(b2Color(c.x, c.y, c.z));
		particlesystem->CreateParticleGroup(pgd);
		return Value();
	}
	ENDDECL4(ph_createparticlecircle, "position,radius,color,flags", "F}:2FF}:4I?", "",
        "creates a circle filled with particles. For flags, see include/physics.lobster");

	STARTDECL(ph_initializeparticles) (VM &, Value &size) {
		CheckParticles(size.fltval());
		return Value();
	}
	ENDDECL1(ph_initializeparticles, "radius", "F", "",
        "initializes the particle system with a given particle radius.");

	STARTDECL(ph_step) (VM &, Value &delta, Value &viter, Value &piter) {
		CheckPhysics();
		world->Step(min(delta.fltval(), 0.1f), viter.intval(), piter.intval());
        if (particlesystem) {
            for (b2Body *body = world->GetBodyList(); body; body = body->GetNext()) {
                for (b2Fixture *fixture = body->GetFixtureList(); fixture;
                     fixture = fixture->GetNext()) {
                    auto pc = ((PhysicsObject *)fixture->GetUserData())->particle_contacts;
                    if (pc) pc->clear();
                }
            }
            auto contacts = particlesystem->GetBodyContacts();
            for (int i = 0; i < particlesystem->GetBodyContactCount(); i++) {
                auto &c = contacts[i];
                auto pc = ((PhysicsObject *)c.fixture->GetUserData())->particle_contacts;
                if (pc) pc->push_back(c.index);
            }
        }
		return Value();
	}
	ENDDECL3(ph_step, "seconds,viter,piter", "FII", "",
        "simulates the physical world for the given period (try: gl_deltatime()). You can specify"
        " the amount of velocity/position iterations per step, more means more accurate but also"
        " more expensive computationally (try 8 and 3).");

    STARTDECL(ph_particlecontacts) (VM &vm, Value &id) {
        CheckPhysics();
        auto &po = GetObject(vm, id);
        if (!po.particle_contacts) po.particle_contacts = new vector<int>();
        auto numelems = (int)po.particle_contacts->size();
        auto v = vm.NewVec(numelems, numelems, TYPE_ELEM_VECTOR_OF_INT);
        for (int i = 0; i < numelems; i++) v->At(i) = Value((*po.particle_contacts)[i]);
        return Value(v);
    }
    ENDDECL1(ph_particlecontacts, "id", "X", "I]",
        "gets the particle indices that are currently contacting a giving physics object."
        " Call after step(). Indices may be invalid after next step().");

    STARTDECL(ph_raycast) (VM &vm, Value &p1, Value &p2, Value &n) {
        CheckPhysics();
        auto p1v = ValueDecToB2(vm, p1);
        auto p2v = ValueDecToB2(vm, p2);
        auto v = vm.NewVec(0, max(n.ival(), (intp)1), TYPE_ELEM_VECTOR_OF_INT);
        if (!particlesystem) return Value(v);
        struct callback : b2RayCastCallback {
            LVector *v;
            VM &vm;
            float ReportParticle(const b2ParticleSystem *, int i, const b2Vec2 &, const b2Vec2 &,
                                 float) {
                v->Push(vm, Value(i));
                return v->len == v->maxl ? -1.0f : 1.0f; 
            }
            float ReportFixture(b2Fixture *, const b2Vec2 &, const b2Vec2 &, float) {
                return -1.0f;
            }
            callback(LVector *_v, VM &vm) : v(_v), vm(vm) {}
        } cb(v, vm);
        particlesystem->RayCast(&cb, p1v, p2v);
        return Value(v);
    }
    ENDDECL3(ph_raycast, "p1,p2,n", "F}:2F}:2I", "I]",
             "returns a vector of the first n particle ids that intersect a ray from p1 to p2,"
             " not including particles that overlap p1.");

    STARTDECL(ph_deleteparticle) (VM &, Value &i) {
        CheckPhysics();
        particlesystem->DestroyParticle(i.intval());
        return Value();
    }
    ENDDECL1(ph_deleteparticle, "i", "I", "",
        "deletes given particle. Deleting particles causes indices to be invalidated at next"
        " step().");

    STARTDECL(ph_getparticleposition) (VM &vm, Value &i) {
        CheckPhysics();
        auto pos = B2ToFloat2(particlesystem->GetPositionBuffer()[i.ival()]);
        return Value(ToValueFLT(vm, pos));
    }
    ENDDECL1(ph_getparticleposition, "i", "I", "F}:2",
             "gets a particle's position.");

    STARTDECL(ph_render) (VM &) {
		CheckPhysics();
		auto oldobject2view = otransforms.object2view;
		auto oldcolor = curcolor;
		for (b2Body *body = world->GetBodyList(); body; body = body->GetNext()) {
			auto pos = body->GetPosition();
			auto mat = translation(float3(pos.x, pos.y, 0)) * rotationZ(body->GetAngle());
            otransforms.object2view = oldobject2view * mat;
			for (b2Fixture *fixture = body->GetFixtureList(); fixture;
                 fixture = fixture->GetNext()) {
				auto shapetype = fixture->GetType();
				auto &r = ((PhysicsObject *)fixture->GetUserData())->r;
				curcolor = r.color;
				switch (shapetype) {
					case b2Shape::e_polygon: {
                        r.Set();
                        auto polyshape = (b2PolygonShape *)fixture->GetShape();
						RenderArraySlow(PRIM_FAN,
                                        make_span(polyshape->m_vertices, polyshape->m_count),
                                        "pn",
							            span<int>(),
                                        make_span(polyshape->m_normals, polyshape->m_count));
						break;
					}
					case b2Shape::e_circle: {
                        r.sh->SetTextures(r.textures);  // FIXME
                        auto polyshape = (b2CircleShape *)fixture->GetShape();
                        Transform2D(translation(float3(B2ToFloat2(polyshape->m_p), 0)), [&]() {
                            geomcache->RenderCircle(r.sh, PRIM_FAN, 20, polyshape->m_radius);
                        });
						break;
					}
					case b2Shape::e_edge:
					case b2Shape::e_chain:
                    case b2Shape::e_typeCount:
						assert(0);
						break;
				}
			}
		}
        otransforms.object2view = oldobject2view;
		curcolor = oldcolor;
		return Value();
	}
	ENDDECL0(ph_render, "", "", "",
        "renders all rigid body objects.");

    STARTDECL(ph_renderparticles) (VM &, Value &particlescale) {
        CheckPhysics();
        if (!particlesystem) return Value();
        //Output(OUTPUT_DEBUG, "rendering particles: ", particlesystem->GetParticleCount());
        auto verts = (float2 *)particlesystem->GetPositionBuffer();
        auto colors = (byte4 *)particlesystem->GetColorBuffer();
        auto scale = length(otransforms.object2view[0].xy());
        SetPointSprite(scale * particlesystem->GetRadius() * particlescale.fltval());
        particlematerial->Set();
        RenderArraySlow(PRIM_POINT,
                        make_span(verts, particlesystem->GetParticleCount()),
                        "pC",
                        span<int>(),
                        make_span(colors, particlesystem->GetParticleCount()));
        return Value();
    }
    ENDDECL1(ph_renderparticles, "scale", "F", "",
        "render all particles, with the given scale.");
}

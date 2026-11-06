/* Subset of the PS2 V/E effect runtime used by em008's teleports.
 * V sequencing: 00206f90/00207380; E scale/color: 001f12e0/001f8ce0.
 * Unsupported P312 and E797 model children are omitted, not replaced by invented sprites. */
typedef struct {const unsigned char *data;float at[3],age;unsigned int fired;} VgFxSequence;
static VgFxSequence vg_fx_sequences[8];
static unsigned int vg_fx_next;
static unsigned int vg_fx_u16(const unsigned char *p) {return p[0]|p[1]<<8;}
static int vg_fx_i16(const unsigned char *p) {return (short)vg_fx_u16(p);}
static float vg_fx_float(const unsigned char *p) {float f;memcpy(&f,p,4);return f;}
static void vg_fx_reset(void) {memset(vg_fx_sequences,0,sizeof(vg_fx_sequences));vg_fx_next=0;}
void fx_vanguard_event(int id,const float at[3]) {
    const unsigned char *data=id==702?vg_fx_V702:id==499?vg_fx_V499:id==516?vg_fx_V516:0;
    VgFxSequence *sequence;
    if(!data)return;
    sequence=&vg_fx_sequences[vg_fx_next++%8];memset(sequence,0,sizeof(*sequence));
    sequence->data=data;memcpy(sequence->at,at,sizeof(sequence->at));
}
static float vg_fx_curve(const unsigned char *e,int axis,float age) {
    float value=vg_fx_float(e+0xa8+axis*4);int i;
    if(!e[0x88+axis])return value;
    for(i=0;i<4;++i) {
        int duration=vg_fx_i16(e+0x8e + i*6+axis*2);
        float target=vg_fx_float(e+0xb4+i*12+axis*4);
        if(duration<0)return value;
        if(duration>0 && age<duration)return value+(target-value)*age/duration;
        age-=duration;value=target;
    }
    return value;
}
static unsigned int vg_fx_color(const unsigned char *e,float age) {
    int i,k;const unsigned char *a=e+0x174,*b=a;float t=0;unsigned int color=0;
    for(i=0;i<3;++i) {
        int duration=vg_fx_i16(a);b=a+20;
        if(duration>0 && age<duration) {t=age/duration;break;}
        age-=duration;a=b;t=0;
    }
    for(k=0;k<4;++k) {
        float value=(a[4+k]+(b[4+k]-a[4+k])*t)*2;
        unsigned int byte=(unsigned int)fmaxf(0,fminf(255,value));color|=byte<<(k*8);
    }
    return color;
}
static void vg_fx_sprite(Sprite *s,float frames) {
    const unsigned char *e=s->native;
    s->native_age+=frames;s->life-=frames/60.0f;
    s->size0=s->size1=vg_fx_float(e+0x3c)*.5f*s->native_scale[0]*vg_fx_curve(e,0,s->native_age);
    s->native_height=vg_fx_float(e+0x44)*.5f*s->native_scale[1]*vg_fx_curve(e,1,s->native_age);
    s->color=vg_fx_color(e,s->native_age);
    s->angle+=vg_fx_float(e+0x14c)*frames*0.0174532925f;
}
static void vg_fx_uv(const Sprite *s,float *u0,float *v0,float *u1,float *v1) {
    const unsigned char *e=s->native;int texture=(int)vg_fx_u16(e+4),x,y,w,h;
    if(texture==25) {
        /* E796/1193 choose frame 0 or 1 of A117 at spawn (001ec360). */
        const unsigned char *frame=vg_fx_A117+6+s->cell*10;
        x=vg_fx_u16(frame);y=vg_fx_u16(frame+2);w=vg_fx_u16(frame+4);h=vg_fx_u16(frame+6);
    } else {x=vg_fx_u16(e+12);y=vg_fx_u16(e+14);w=vg_fx_u16(e+16);h=vg_fx_u16(e+18);}
    *u0=x/(float)(texture==25?256:128);*v0=y/(float)(texture==25?256:128);
    *u1=(x+w)/(float)(texture==25?256:128);*v1=(y+h)/(float)(texture==25?256:128);
}
static void vg_fx_update(float frames) {
    int i,j,k;
    for(i=0;i<8;++i) {
        VgFxSequence *sequence=&vg_fx_sequences[i];
        if(!sequence->data)continue;
        sequence->age+=frames;
        for(j=0;j<(int)vg_fx_u16(sequence->data);++j) {
            const unsigned char *child=sequence->data+4+j*44,*e=0;
            int id=(int)vg_fx_u16(child+2);float at[3];Sprite *s;
            if(sequence->fired&(1u<<j) || sequence->age<=vg_fx_i16(child+4))continue;
            sequence->fired|=1u<<j;
            if(child[0]!=1)continue;
            if(id==796)e=vg_fx_E796;else if(id==1193)e=vg_fx_E1193;
            else if(id==1311)e=vg_fx_E1311;else if(id==1312)e=vg_fx_E1312;
            if(!e)continue;
            for(k=0;k<3;++k) {
                float spread=vg_fx_float(e+0x5c+k*4);
                at[k]=sequence->at[k]+vg_fx_float(child+8+k*4)+(fx_rand()-.5f)*spread*vg_fx_float(child+32+k*4);
            }
            s=spawn(FX_SHEET_COUNT+(vg_fx_u16(e+4)==48),1,0,at,0,0,vg_fx_u16(e+0x80)/60.0f,0);
            s->native=e;s->cell=(unsigned char)(fx_rand()>=.5f);
            s->angle=fx_rand()*6.2831853f;
            for(k=0;k<3;++k)s->native_scale[k]=vg_fx_float(child+32+k*4);
            vg_fx_sprite(s,0);
        }
        if(sequence->fired==((1u<<vg_fx_u16(sequence->data))-1))sequence->data=0;
    }
}

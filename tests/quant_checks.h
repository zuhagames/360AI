// Encode known integer weights and scales, then verify the native decoder over
// three blocks. This catches signed offsets, packed bitplanes and block strides.
static void put_half(u8* dst,f32 f){ u16 h=gx_le16(gx_f32_to_f16(f)); memcpy(dst,&h,2); }
static void quant_checks(){
  const int types[]={GXQ_Q4_0,GXQ_Q4_1,GXQ_Q5_0,GXQ_Q5_1,GXQ_Q8_0,GXQ_Q8_1,
    GXQ_Q2_K,GXQ_Q3_K,GXQ_Q4_K,GXQ_Q5_K,GXQ_Q6_K,GXQ_Q8_K};
  for(size_t t=0;t<sizeof(types)/sizeof(types[0]);++t){
    int type=types[t]; size_t count=gxq_block_elems(type), bytes=gxq_type_size(type);
    std::vector<u8> data(bytes*3,0); std::vector<f32> expected(count*3),actual(count*3+4,12345);
    for(size_t b=0;b<3;++b){
      u8* p=&data[b*bytes]; f32 d=(f32)(b+1)*0.5f, dm=0.25f;
      if(type<=GXQ_Q8_1){
        put_half(p,d);
        if(type==GXQ_Q4_1 || type==GXQ_Q5_1 || type==GXQ_Q8_1) put_half(p+2,dm);
        for(size_t i=0;i<count;++i){
          int raw=(int)((i*7+b*3)%32);
          if(type==GXQ_Q4_0 || type==GXQ_Q4_1){
            raw&=15; size_t offset=type==GXQ_Q4_0 ? 2 : 4;
            p[offset+i%16]|=(u8)(raw<<(i/16*4));
            expected[b*count+i]=d*(type==GXQ_Q4_0 ? raw-8 : raw)+(type==GXQ_Q4_1 ? dm : 0);
          } else if(type==GXQ_Q5_0 || type==GXQ_Q5_1){
            size_t offset=type==GXQ_Q5_0 ? 2 : 4;
            p[offset+i/8]|=(u8)((raw>>4)<<(i%8));
            p[offset+4+i%16]|=(u8)((raw&15)<<(i/16*4));
            expected[b*count+i]=d*(type==GXQ_Q5_0 ? raw-16 : raw)+(type==GXQ_Q5_1 ? dm : 0);
          } else {
            int q=(int)i-16; p[(type==GXQ_Q8_0 ? 2 : 4)+i]=(u8)(i8)q;
            expected[b*count+i]=d*q;
          }
        }
      } else if(type==GXQ_Q2_K || type==GXQ_Q3_K){
        if(type==GXQ_Q2_K){
          put_half(p+80,d); put_half(p+82,dm);
          for(size_t g=0;g<16;++g) p[g]=(u8)((g&15)|(((g*3)%16)<<4));
        } else {
          put_half(p+108,d);
          for(size_t g=0;g<16;++g){
            int scale=(int)(g*3+5);
            p[96+g%8]|=(u8)((scale&15)<<(4*(g/8)));
            p[104+g%4]|=(u8)((scale>>4)<<(2*(g/4)));
          }
        }
        for(size_t i=0;i<count;++i){
          int q=(int)((i+b*3)%8)-4; size_t g=i/16;
          if(type==GXQ_Q2_K){
            q&=3; p[16+(i/128)*32+i%32]|=(u8)(q<<(2*((i%128)/32)));
            expected[b*count+i]=d*(g&15)*q-dm*((g*3)%16);
          } else {
            p[32+(i/128)*32+i%32]|=(u8)((q&3)<<(2*((i%128)/32)));
            if(q>=0) p[i%32]|=(u8)(1<<(i/32));
            expected[b*count+i]=d*((int)(g*3+5)-32)*q;
          }
        }
      } else if(type==GXQ_Q4_K || type==GXQ_Q5_K){
        put_half(p,d); put_half(p+2,dm); int scales[8],mins[8];
        for(int g=0;g<8;++g){ scales[g]=(g*7+3)%64; mins[g]=(g*11+5)%64; }
        for(int g=0;g<4;++g){
          p[4+g]=(u8)(scales[g]|((scales[g+4]>>4)<<6));
          p[8+g]=(u8)(mins[g]|((mins[g+4]>>4)<<6));
          p[12+g]=(u8)((scales[g+4]&15)|((mins[g+4]&15)<<4));
        }
        for(size_t i=0;i<count;++i){
          int raw=(int)((i*7+b)% (type==GXQ_Q4_K ? 16 : 32));
          p[(type==GXQ_Q4_K ? 16 : 48)+(i/64)*32+i%32]|=(u8)((raw&15)<<(i%64<32 ? 0 : 4));
          if(type==GXQ_Q5_K) p[16+i%32]|=(u8)((raw>>4)<<(i/32));
          expected[b*count+i]=d*scales[i/32]*raw-dm*mins[i/32];
        }
      } else if(type==GXQ_Q6_K){
        put_half(p+208,d);
        for(size_t g=0;g<16;++g) p[192+g]=(u8)(i8)((int)g-8);
        for(size_t i=0;i<count;++i){
          int raw=(int)((i*13+b)%64);
          p[(i/128)*64+i%64]|=(u8)((raw&15)<<(i%128<64 ? 0 : 4));
          p[128+(i/128)*32+i%32]|=(u8)((raw>>4)<<(2*((i%128)/32)));
          expected[b*count+i]=d*((int)(i/16)-8)*(raw-32);
        }
      } else {
        u32 bits=gx_le32(gx_bits_from_f32(d)); memcpy(p,&bits,4);
        for(size_t i=0;i<count;++i){ int q=(int)((i+b*3)%255)-127; p[4+i]=(u8)(i8)q; expected[b*count+i]=d*q; }
      }
    }
    gxq_dequant_row(type,&data[0],&actual[0],count*3);
    for(size_t i=0;i<expected.size();++i) assert(actual[i]==expected[i]);
    for(size_t i=expected.size();i<actual.size();++i) assert(actual[i]==12345);
  }
}

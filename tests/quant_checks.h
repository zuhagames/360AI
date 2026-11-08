// Encode known integer weights and scales, then verify the native decoder over
// three blocks. This catches signed offsets, packed bitplanes and block strides.
static void put_half(u8* dst,f32 f){ u16 h=gx_le16(gx_f32_to_f16(f)); memcpy(dst,&h,2); }
static void quant_checks(){
  const u32 widths[]={32,96,576,640,2048};
  for(int format=0;format<2;++format) for(size_t w=0;w<5;++w){
    u32 cols=widths[w],stride=(cols/32)*(format ? 36 : 34),rows=257;
    std::vector<u8> weights(1+(size_t)stride*rows);std::vector<f32> input(cols),scratch(cols),decoded(cols),out(rows);
    for(u32 i=0;i<cols;++i) input[i]=((int)((i*31)%47)-23)*0.0625f;
    for(u32 r=0;r<rows;++r) for(u32 b=0;b<cols/32;++b){
      u8* p=&weights[1+(size_t)r*stride+b*(format ? 36 : 34)];
      put_half(p,((int)((r+b)%7)-3)*0.0009765625f);
      for(u32 j=0;j<32;++j) p[(format ? 4 : 2)+j]=(u8)((r*71+b*13+j*17)%256);
    }
    int type=format ? GXQ_Q8_1 : GXQ_Q8_0;
    gx_matvec_rows(&weights[1],type,cols,stride,&input[0],&out[0],rows,&scratch[0]);
    for(u32 r=0;r<rows;++r){
      gxq_dequant_row(type,&weights[1+(size_t)r*stride],&decoded[0],cols);
      double expected=0;for(u32 c=0;c<cols;++c) expected+=(double)decoded[c]*input[c];
      assert(fabs(out[r]-expected)<0.0001+fabs(expected)*0.00001);
    }
  }
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
    // Matrix kernels must agree with independent decoded weights, including
    // multiple Q8 blocks, signed extrema, odd row alignment and both strides.
    const size_t rows=3,cols=count*3;
    std::vector<u8> matrix(data.size()*rows+1,0);
    for(size_t r=0;r<rows;++r) memcpy(&matrix[1+r*data.size()],&data[0],data.size());
    std::vector<f32> input(cols),scratch(cols);f32 result[3];
    for(size_t i=0;i<cols;++i) input[i]=((int)((i*17)%23)-11)*0.03125f;
    gx_matvec_rows(&matrix[1],type,(u32)cols,(u32)data.size(),&input[0],result,rows,&scratch[0]);
    double dot=0;for(size_t i=0;i<cols;++i) dot+=(double)expected[i]*input[i];
    for(size_t r=0;r<rows;++r) assert(fabs(result[r]-dot)<0.0001+fabs(dot)*0.00001);
  }
}

static void matrix_speed_checks(){
  // Distinct, unaligned Q8 rows exercise the paired candidate, the 256-row
  // calibration sample, parallel-size dispatch and the unpaired final row.
  for(int type=GXQ_Q8_0;type<=GXQ_Q8_1;++type){
    const u32 cols=96,rows=259,block=(u32)gxq_type_size(type),stride=3*block;
    std::vector<u8> matrix(1+rows*stride);std::vector<f32> x(cols),scratch(cols),out(rows+1,12345),decoded(cols);
    for(u32 i=0;i<cols;++i) x[i]=(int(i%17)-8)*0.03125f;
    for(u32 r=0;r<rows;++r) for(u32 b=0;b<3;++b){
      u8* p=&matrix[1+r*stride+b*block];put_half(p,(int((r+b)%11)-5)*0.015625f);
      for(u32 i=0;i<32;++i) p[(type==GXQ_Q8_0 ? 2 : 4)+i]=(u8)(i8)(int((i*17+r*3+b)%256)-128);
    }
    gx_matvec_rows(&matrix[1],type,cols,stride,&x[0],&out[0],rows,&scratch[0]);
    for(u32 r=0;r<rows;++r){
      gxq_dequant_row(type,&matrix[1+r*stride],&decoded[0],cols);double expected=0;
      for(u32 i=0;i<cols;++i) expected+=(double)decoded[i]*x[i];
      assert(fabs(out[r]-expected)<0.0001+fabs(expected)*0.00001);
    }
    assert(out[rows]==12345);
  }
  const u32 dimensions[]={1,3,4,15,16,17,64,172};
  for(size_t d=0;d<sizeof(dimensions)/sizeof(dimensions[0]);++d){
    u32 cols=dimensions[d],stride=(cols+1)*4;const u32 rows=3;
    std::vector<u8> matrix(1+rows*stride);std::vector<f32> x(cols),scratch(cols);f32 out[3];
    for(u32 i=0;i<cols;++i) x[i]=(int(i%13)-6)*0.0625f;
    for(u32 r=0;r<rows;++r) for(u32 i=0;i<cols;++i){
      u32 bits=gx_le32(gx_bits_from_f32((int((i+r*3)%19)-9)*0.03125f));memcpy(&matrix[1+r*stride+i*4],&bits,4);
    }
    gx_matvec_rows(&matrix[1],GXQ_F32,cols,stride,&x[0],out,rows,&scratch[0]);
    for(u32 r=0;r<rows;++r){
      double expected=0;for(u32 i=0;i<cols;++i) expected+=(int((i+r*3)%19)-9)*0.03125*x[i];
      assert(fabs(out[r]-expected)<0.00001);
    }
  }
}

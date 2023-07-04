// Copyright (C) Codeplay Software Limited
//
// Licensed under the Apache License, Version 2.0 (the "License") with LLVM
// Exceptions; you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://github.com/codeplaysoftware/oneapi-construction-kit/blob/main/LICENSE.txt
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <compiler/utils/mangling.h>
#include <compiler/utils/target_extension_types.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/SourceMgr.h>
#include <multi_llvm/llvm_version.h>

#include <cstdint>
#include <cstring>

#include "common.h"
#include "compiler/module.h"

using namespace compiler::utils;

struct ManglingTest : ::testing::Test {
  void SetUp() override {}

  std::unique_ptr<llvm::Module> parseModule(llvm::StringRef Assembly) {
    llvm::SMDiagnostic Error;
    auto M = llvm::parseAssemblyString(Assembly, Error, Context);

    std::string ErrMsg;
    llvm::raw_string_ostream OS(ErrMsg);
    Error.print("", OS);
    EXPECT_TRUE(M) << OS.str();

    return M;
  }

  llvm::LLVMContext Context;
};

TEST_F(ManglingTest, MangleBuiltinTypes) {
  // With opaque pointers, before LLVM 17 we can't actually mangle OpenCL
  // builtin types because our APIs don't expose the ability to mangle a pointer
  // based on its element type.
  // This is never a problem in the compiler as we don't generate such functions
  // on the fly, but it is a weakness in the API. We could fix this, or wait it
  // out until LLVM 17 becomes the minimum version, at which point target
  // extension types save the day.
#if LLVM_VERSION_LESS(17, 0)
  GTEST_SKIP();
#else
  NameMangler Mangler(&Context);

  std::pair<llvm::Type *, const char *> TypesToMangle[] = {
      {tgtext::getEventTy(Context), "9ocl_event"},
      {tgtext::getSamplerTy(Context), "11ocl_sampler"},
      {tgtext::getImage1DTy(Context), "11ocl_image1d"},
      {tgtext::getImage1DTy(Context), "11ocl_image1d"},
      {tgtext::getImage2DTy(Context), "11ocl_image2d"},
      {tgtext::getImage3DTy(Context), "11ocl_image3d"},
      {tgtext::getImage1DArrayTy(Context), "16ocl_image1darray"},
      {tgtext::getImage1DBufferTy(Context), "17ocl_image1dbuffer"},
      {tgtext::getImage2DArrayTy(Context), "16ocl_image2darray"},
      {tgtext::getImage2DTy(Context, /*Depth*/ true, /*MS*/ false),
       "16ocl_image2ddepth"},
      {tgtext::getImage2DTy(Context, /*Depth*/ false, /*MS*/ true),
       "15ocl_image2dmsaa"},
      {tgtext::getImage2DTy(Context, /*Depth*/ true, /*MS*/ true),
       "20ocl_image2dmsaadepth"},
      {tgtext::getImage2DArrayTy(Context, /*Depth*/ true, /*MS*/ false),
       "21ocl_image2darraydepth"},
      {tgtext::getImage2DArrayTy(Context, /*Depth*/ false, /*MS*/ true),
       "20ocl_image2darraymsaa"},
      {tgtext::getImage2DArrayTy(Context, /*Depth*/ true, /*MS*/ true),
       "25ocl_image2darraymsaadepth"},
  };

  std::string Name;
  llvm::raw_string_ostream OS(Name);

  for (auto &[Ty, ExpName] : TypesToMangle) {
    Name.clear();
    EXPECT_TRUE(Mangler.mangleType(OS, Ty, TypeQualifiers{}));
    EXPECT_EQ(Name, ExpName);
  }
#endif
}

TEST_F(ManglingTest, DemangleImage1DTy) {
  auto M = parseModule(R"(
  declare void @_Z4test11ocl_image1d(ptr %img)
  )");

  NameMangler Mangler(&Context);

  auto *F = M->getFunction("_Z4test11ocl_image1d");
  EXPECT_TRUE(F);

  llvm::SmallVector<llvm::Type *> Tys;
  llvm::SmallVector<TypeQualifiers> Quals;
  auto DemangledName = Mangler.demangleName(F->getName(), Tys, Quals);
  EXPECT_EQ(DemangledName, "test");

  EXPECT_EQ(Tys.size(), 1);
  EXPECT_EQ(Quals.size(), 1);

  auto *ImgTy = Tys[0];
  EXPECT_TRUE(ImgTy);

#if LLVM_VERSION_GREATER_EQUAL(17, 0)
  EXPECT_TRUE(ImgTy->isTargetExtTy());
  auto *TgtTy = llvm::cast<llvm::TargetExtType>(ImgTy);
  EXPECT_EQ(TgtTy->getName(), "spirv.Image");
  EXPECT_EQ(TgtTy->getIntParameter(tgtext::ImageTyDimensionalityIdx),
            tgtext::ImageDim1D);
  EXPECT_EQ(TgtTy->getIntParameter(tgtext::ImageTyDepthIdx),
            tgtext::ImageDepthNone);
  EXPECT_EQ(TgtTy->getIntParameter(tgtext::ImageTyArrayedIdx),
            tgtext::ImageNonArrayed);
  EXPECT_EQ(TgtTy->getIntParameter(tgtext::ImageTyMSIdx),
            tgtext::ImageMSSingleSampled);
  EXPECT_EQ(TgtTy->getIntParameter(tgtext::ImageTySampledIdx),
            tgtext::ImageSampledRuntime);
  EXPECT_EQ(TgtTy->getIntParameter(tgtext::ImageTyAccessQualIdx),
            tgtext::ImageAccessQualReadOnly);
#else
  EXPECT_TRUE(ImgTy->isStructTy());
  EXPECT_EQ(llvm::cast<llvm::StructType>(ImgTy)->getName(), "opencl.image1d_t");
#endif
}

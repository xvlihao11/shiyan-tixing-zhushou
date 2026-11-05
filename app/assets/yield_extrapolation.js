// Edge-segment extrapolation is an explicit estimate, never a measured point.
function yieldCurveEstimate(points,input,value,output){
  if(!Array.isArray(points)||points.length<2||!Number.isFinite(value))return {error:'至少需要两个有效实测点'};
  if(!points.every((p,i)=>Number.isFinite(p.y)&&Number.isFinite(p.p)&&(i===0||p.y>points[i-1].y&&p.p>points[i-1].p)))return {error:'实测点不满足递增趋势，无法预测'};
  const valid=(key,n)=>key==='y'?n>0&&n<=100:n>0&&n<=1000;
  if(!valid(input,value))return {error:input==='y'?'收率须大于 0 且不超过 100%':'目标针入度须大于 0 且不超过 1000'};
  const first=points[0],last=points.at(-1),below=value<first[input],above=value>last[input],extrapolated=below||above;
  let result,edgeDistance=0,edgeSpan=0;
  if(extrapolated){const a=below?points[0]:points.at(-2),b=below?points[1]:last;result=a[output]+(value-a[input])*(b[output]-a[output])/(b[input]-a[input]);edgeDistance=below?first[input]-value:value-last[input];edgeSpan=b[input]-a[input]}
  else result=yieldLocalInterpolate(points,input,value,output);
  if(!Number.isFinite(result)||!valid(output,result))return {error:'按端部趋势推算的结果超出有效物理范围，请补充实测点后再估算'};
  return {value:result,extrapolated,edgeDistance,edgeSpan,pointCount:points.length};
}
function yieldEstimateNote(estimate,input){return estimate.extrapolated?`外推估算：超出最近实测端点 ${yieldFmt(estimate.edgeDistance)} ${input==='p'?'针':'个收率百分点'}，沿端部最近两点趋势延伸。距离越远，需更多实测验证；此值不是实测结果。`:'实测范围内插值。'}
